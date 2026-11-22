#include "bsp_overlay.h"
#include "bsp_file.h"
#include "bsp_props.h"
#include "bsp_seam.h"
#include "bsp_seam_filter.h"
#include "extension.h"

#include "bspflags.h"
#include "const.h"
#include "datacache/imdlcache.h"
#include "engine/IEngineTrace.h"
#include "gametrace.h"
#include "tier1/checksum_crc.h"
#include <IPlayerHelpers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// tier0 (not in the SDK's headers): sets this thread's g_nThreadID to a free id, and gives it back.
extern "C" int AllocateThreadID();
extern "C" void FreeThreadID();

namespace BSPOverlay
{
	namespace
	{
		constexpr int kFloats = BSPSeam::EDGE_POINT_FLOATS;
		constexpr float kSlide = 16.0f; // half a hull: how far a point is tested toward its neighbour at a side wall
		constexpr uint32_t kFormat = 1;
		constexpr int64_t kIdleMs = 50; // no game frame and no overlay call for this long, and hibernating: the worker may run

		struct Points
		{
			std::vector<float> rows;
			uint32_t crc = 0;

			int Count() const
			{
				return (int)(rows.size() / kFloats);
			}

			const float *Row(int i) const
			{
				return &rows[(size_t)i * kFloats];
			}
		};

		struct Pass
		{
			float values[4];
			float spacing;
			std::vector<int> kinds; // -1 untested
			std::vector<int> order;
			size_t next = 0;
			bool saved = false;

			bool Done() const
			{
				return next == order.size();
			}
		};

		struct QueueEntry
		{
			float values[4];
			float spacing;
			bool done; // on this map: loaded or finished
		};

		// The worker holds it for one point; the game thread for every overlay call, at each frame's start and at a client's connect.
		std::mutex g_work;
		std::thread g_thread;
		std::atomic<bool> g_stop {false};
		std::atomic<int64_t> g_lastFrame {0};
		// CBaseServer::m_bHibernating of the game server; null: the worker never runs.
		const volatile uint8_t *g_hibernating = nullptr;
		IMDLCache *g_mdlcache = nullptr;

		bool g_mapReady = false;
		std::string g_map, g_cacheDir;
		uint32_t g_bspKey = 0, g_buildKey = 0;
		bool g_cacheOk = false;
		bool g_warnedSave = false;

		// This map's points per spacing and passes per (values, spacing); g_active is the one the natives step and read.
		std::map<float, Points> g_points;
		std::list<Pass> g_passes;
		Pass *g_active = nullptr;
		std::vector<QueueEntry> g_queue;

		int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		struct Crc
		{
			CRC32_t c;

			Crc()
			{
				CRC32_Init(&c);
			}

			void Add(const void *p, size_t n)
			{
				CRC32_ProcessBuffer(&c, p, (int)n);
			}

			uint32_t Done()
			{
				CRC32_t r = c;
				CRC32_Final(&r);
				return (uint32_t)r;
			}
		};

		bool SameValues(const float a[4], const float b[4])
		{
			return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
		}

		Pass *FindPass(const float values[4], float spacing)
		{
			for (Pass &p : g_passes)
			{
				if (p.spacing == spacing && SameValues(p.values, values))
				{
					return &p;
				}
			}
			return nullptr;
		}

		const Points &PointsOf(const Pass &p)
		{
			return g_points.at(p.spacing);
		}

		// The kinds of mask caught at point row moved shift units along its edge.
		// Pixelsurf needs the wall to go on past the edge; head seam, ceiling and texturebug come from bottom edges.
		int TestAt(const float *row, float shift, int mask, const float v[4])
		{
			const float wall[3] = {row[0] - row[4] * shift, row[1] + row[3] * shift, row[2]};
			const float flat[3] = {row[3], row[4], 0.0f};
			int bits = (int)std::floor(row[6]);
			int kinds = 0;
			// A covered top edge's end: only the exact-position pixelwalk; its stretch runs 1u along the edge from it.
			if (bits & BSPSeam::EDGE_END)
			{
				const float along[2] = {row[9] - row[7], row[10] - row[8]};
				float out[3];
				if ((mask & KIND_PIXELWALK_EXACT) && BSPSeam::PixelwalkEnd(wall, flat, along, row[2], out) > 0)
				{
					kinds |= KIND_PIXELWALK_EXACT;
				}
				return kinds & mask;
			}
			if ((mask & (KIND_PIXELWALK | KIND_PIXELWALK_EXACT)) && (bits & BSPSeam::EDGE_TOP))
			{
				float pw[4];
				int f = BSPSeam::PixelwalkCatch(wall, flat, row[2], pw);
				if (f > 0)
				{
					kinds |= (f & BSPSeam::CATCH_EXACT) ? KIND_PIXELWALK_EXACT : KIND_PIXELWALK;
				}
			}
			float speed[2], start[6];
			if ((mask & KIND_PIXELSURF) && (bits & BSPSeam::EDGE_CONTINUES)
				&& BSPSeam::Catch(wall, flat, row[2], v[0], v[1], v[2], v[3], speed, start, false) > 0)
			{
				kinds |= KIND_PIXELSURF;
			}
			if (!(bits & BSPSeam::EDGE_TOP))
			{
				if (mask & (KIND_HEADSEAM | KIND_CEILING))
				{
					int h = BSPSeam::Catch(wall, flat, row[2], v[0], v[1], v[2], v[3], speed, start, true);
					if (h > 0 && (h & (BSPSeam::CATCH_STANDING | BSPSeam::CATCH_DUCKED)))
					{
						kinds |= KIND_HEADSEAM;
					}
					else if (h > 0 && (h & BSPSeam::CATCH_UNDER_CEILING))
					{
						kinds |= KIND_CEILING;
					}
				}
				float info[2];
				if ((mask & KIND_TEXTUREBUG) && BSPSeam::TexturebugCatch(wall, flat, row[2], v[0], v[1], v[3], info) > 0)
				{
					kinds |= KIND_TEXTUREBUG;
				}
			}
			return kinds & mask;
		}

		// A one-unit slice of a hull against the face from z up starts solid on what blocks movement: a side wall is there.
		bool HullBlocked(const float *row, float z)
		{
			float out = 16.0f * (std::fabs(row[3]) + std::fabs(row[4])) + 0.1f;
			Vector hull(row[0] + row[3] * out, row[1] + row[4] * out, z);
			Ray_t ray;
			ray.Init(hull, hull, Vector(-16.0f, -16.0f, 0.0f), Vector(16.0f, 16.0f, 1.0f));
			BSPSeam::MovementFilter filter;
			CGameTrace tr;
			BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &filter, &tr);
			return tr.startsolid;
		}

		void TestToward(Pass &p, const Points &pts, int miss, int other, int kinds)
		{
			if (!kinds)
			{
				return;
			}
			const float *a = pts.Row(miss), *b = pts.Row(other);
			// A side wall at the edge: just above it where the hull stands over the edge (pixelsurf, pixelwalk), just under it for the rest.
			int px = kinds & (KIND_PIXELSURF | KIND_PIXELWALK | KIND_PIXELWALK_EXACT), rest = kinds & ~px;
			kinds = 0;
			if (px && HullBlocked(a, a[2] + 0.04f))
			{
				kinds |= px;
			}
			if (rest && HullBlocked(a, a[2] - 1.04f))
			{
				kinds |= rest;
			}
			if (!kinds)
			{
				return;
			}
			float dir = (b[0] - a[0]) * -a[4] + (b[1] - a[1]) * a[3] > 0.0f ? 1.0f : -1.0f;
			int got = 0;
			for (float s = kSlide * 0.5f; s <= kSlide && (kinds & ~got); s += kSlide * 0.5f)
			{
				got |= TestAt(a, dir * s, kinds & ~got, p.values);
			}
			p.kinds[miss] |= got;
		}

		// Where the face goes on behind a side wall, a hull against a point can be inside that wall and miss what its neighbour catches.
		// Moved up to kSlide toward the neighbour it still covers the point: so a beam reaches the side wall.
		void TestEnds(Pass &p, const Points &pts, int i)
		{
			const float *ri = pts.Row(i);
			for (int j = i - 1; j <= i + 1; j += 2)
			{
				if (j < 0 || j >= pts.Count() || pts.Row(j)[5] != ri[5])
				{
					continue;
				}
				// An edge's end point stands alone.
				if (((int)std::floor(ri[6]) | (int)std::floor(pts.Row(j)[6])) & BSPSeam::EDGE_END)
				{
					continue;
				}
				int ki = p.kinds[i], kj = p.kinds[j];
				if (ki < 0 || kj < 0)
				{
					continue;
				}
				TestToward(p, pts, j, i, ki & ~kj);
				TestToward(p, pts, i, j, kj & ~ki);
			}
		}

		void RunOne(Pass &p)
		{
			const Points &pts = PointsOf(p);
			int i = p.order[p.next++];
			p.kinds[i] = TestAt(pts.Row(i), 0.0f, -1, p.values);
			TestEnds(p, pts, i);
		}

		const Points *EnsurePoints(float spacing)
		{
			auto it = g_points.find(spacing);
			if (it != g_points.end())
			{
				return &it->second;
			}
			Points &pts = g_points[spacing];
			int total = BSPSeam::WallEdgePoints(spacing, 0, nullptr, 0);
			pts.rows.assign((size_t)total * kFloats, 0.0f);
			if (total > 0)
			{
				BSPSeam::WallEdgePoints(spacing, 0, pts.rows.data(), total);
			}
			Crc crc;
			crc.Add(pts.rows.data(), pts.rows.size() * sizeof(float));
			pts.crc = crc.Done();
			return &pts;
		}

		// Untested points nearest origin first (null: in point order).
		void Order(Pass &p, const float *origin)
		{
			const Points &pts = PointsOf(p);
			std::vector<std::pair<float, int>> left;
			for (int i = 0; i < pts.Count(); ++i)
			{
				if (p.kinds[i] != -1)
				{
					continue;
				}
				float d = 0.0f;
				if (origin)
				{
					const float *r = pts.Row(i);
					d = (r[0] - origin[0]) * (r[0] - origin[0]) + (r[1] - origin[1]) * (r[1] - origin[1]) + (r[2] - origin[2]) * (r[2] - origin[2]);
				}
				left.push_back({d, i});
			}
			std::stable_sort(left.begin(), left.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
			p.order.clear();
			for (const auto &l : left)
			{
				p.order.push_back(l.second);
			}
			p.next = 0;
		}

		std::string CachePath(const Pass &p)
		{
			Crc key;
			key.Add(&kFormat, sizeof(kFormat));
			key.Add(&g_buildKey, sizeof(g_buildKey));
			key.Add(&p.spacing, sizeof(p.spacing));
			key.Add(p.values, 4 * sizeof(float));
			char name[64];
			snprintf(name, sizeof(name), "_%08x_%08x.bin", g_bspKey, key.Done());
			return g_cacheDir + "/" + g_map + name;
		}

		// "BSPO", format, BSP key, build key, spacing, values, point count, points' CRC; a kind per point follows it.
		std::string Header(const Pass &p)
		{
			const Points &pts = PointsOf(p);
			std::string h("BSPO", 4);
			auto put = [&h](const void *v, size_t n) { h.append(static_cast<const char *>(v), n); };
			uint32_t count = (uint32_t)pts.Count();
			put(&kFormat, 4);
			put(&g_bspKey, 4);
			put(&g_buildKey, 4);
			put(&p.spacing, 4);
			put(p.values, 16);
			put(&count, 4);
			put(&pts.crc, 4);
			return h;
		}

		// A finished pass stays in memory for the map whether or not it could be saved.
		void Save(Pass &p)
		{
			p.saved = true;
			if (!g_cacheOk)
			{
				return;
			}
			std::string path = CachePath(p);
			// Per process: servers sharing the folder don't write into each other's file.
			char pid[32];
			snprintf(pid, sizeof(pid), ".%ld.tmp", (long)getpid());
			std::string tmp = path + pid;
			FILE *f = fopen(tmp.c_str(), "wb");
			bool ok = f != nullptr;
			if (f)
			{
				std::string h = Header(p);
				ok = fwrite(h.data(), 1, h.size(), f) == h.size() && fwrite(p.kinds.data(), sizeof(int), p.kinds.size(), f) == p.kinds.size();
				ok = fclose(f) == 0 && ok;
			}
			if (ok)
			{
				remove(path.c_str());
				ok = rename(tmp.c_str(), path.c_str()) == 0;
			}
			if (!ok)
			{
				remove(tmp.c_str());
				if (!g_warnedSave)
				{
					g_warnedSave = true;
					smutils->LogError(myself, "Seam overlay pass not saved to %s", path.c_str());
				}
			}
		}

		bool Load(Pass &p)
		{
			if (!g_cacheOk)
			{
				return false;
			}
			FILE *f = fopen(CachePath(p).c_str(), "rb");
			if (!f)
			{
				return false;
			}
			std::string want = Header(p), got(want.size(), '\0');
			std::vector<int> kinds(PointsOf(p).Count());
			bool ok = fread(&got[0], 1, got.size(), f) == got.size() && got == want
					  && fread(kinds.data(), sizeof(int), kinds.size(), f) == kinds.size() && fgetc(f) == EOF;
			fclose(f);
			if (!ok || std::any_of(kinds.begin(), kinds.end(), [](int k) { return k < 0 || k > 63; }))
			{
				return false;
			}
			p.kinds = std::move(kinds);
			p.order.clear();
			p.next = 0;
			p.saved = true;
			return true;
		}

		Pass &NewPass(const float values[4], float spacing)
		{
			g_passes.emplace_back();
			Pass &p = g_passes.back();
			std::copy(values, values + 4, p.values);
			p.spacing = spacing;
			p.kinds.assign(g_points.at(spacing).Count(), -1);
			return p;
		}

		// The worker's next point: the active pass, then the queued values not done on this map. False when there is none.
		bool WorkOne()
		{
			if (!g_mapReady)
			{
				return false;
			}
			if (g_active && !g_active->Done())
			{
				RunOne(*g_active);
				if (g_active->Done())
				{
					Save(*g_active);
				}
				return true;
			}
			for (QueueEntry &q : g_queue)
			{
				if (q.done)
				{
					continue;
				}
				if (!EnsurePoints(q.spacing)->Count())
				{
					q.done = true;
					continue;
				}
				Pass *p = FindPass(q.values, q.spacing);
				if (!p)
				{
					p = &NewPass(q.values, q.spacing);
					if (!Load(*p))
					{
						Order(*p, nullptr);
					}
				}
				if (p->Done())
				{
					q.done = true;
					continue;
				}
				RunOne(*p);
				if (p->Done())
				{
					Save(*p);
					q.done = true;
				}
				return true;
			}
			return false;
		}

		bool MayRun()
		{
			return g_hibernating && *g_hibernating && NowMs() - g_lastFrame >= kIdleMs;
		}

		void WorkerMain()
		{
			// Per-thread engine state (datacache's frame locks) is indexed by tier0's thread id;
			// 0, a thread without one, is the main thread's.
			AllocateThreadID();
			while (!g_stop)
			{
				if (!MayRun())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}
				std::unique_lock<std::mutex> lk(g_work);
				if (g_stop || !MayRun())
				{
					continue;
				}
				// Model data the traces read stays cached until EndLock, as the main thread's frame lock keeps it.
				if (g_mdlcache)
				{
					g_mdlcache->BeginLock();
				}
				bool worked = WorkOne();
				if (g_mdlcache)
				{
					g_mdlcache->EndLock();
				}
				if (!worked)
				{
					lk.unlock();
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
			}
			FreeThreadID();
		}

		void OnGameFrame(bool)
		{
			g_lastFrame = NowMs();
			std::lock_guard<std::mutex> lk(g_work);
		}

		// A connecting client wakes the server: the engine clears its hibernation flag first,
		// and the worker's point ends before the client's entity is made.
		class ConnectListener : public IClientListener
		{
		public:
			bool InterceptClientConnect(int, char *, size_t) override
			{
				auto lk = GameThreadLock();
				return true;
			}
		} g_connect;

		void MakeDirs(const std::string &path)
		{
			for (size_t k = 1; k <= path.size(); ++k)
			{
				if (k == path.size() || path[k] == '/' || path[k] == '\\')
				{
					std::string dir = path.substr(0, k);
#ifdef _WIN32
					_mkdir(dir.c_str());
#else
					mkdir(dir.c_str(), 0775);
#endif
				}
			}
		}

		bool FileCrc(const char *path, uint32_t &out)
		{
			FILE *f = fopen(path, "rb");
			if (!f)
			{
				return false;
			}
			Crc crc;
			std::vector<char> buf(1 << 16);
			size_t n;
			while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
			{
				crc.Add(buf.data(), n);
			}
			fclose(f);
			out = crc.Done();
			return true;
		}

		// The lump directory, the size and the entity lump: a recompile moves lumps, an entity edit changes the entity lump.
		bool BspKey(const char *path, uint32_t &out)
		{
			FILE *f = fopen(path, "rb");
			if (!f)
			{
				return false;
			}
			BSPFile::Header header = {};
			bool ok = BSPFile::ReadHeader(f, header);
			BSPFile::Lump entities = header.LumpAt(0);
			ok = ok && entities.fileofs > 0 && entities.filelen >= 0 && fseek(f, 0, SEEK_END) == 0;
			long size = ok ? ftell(f) : 0;
			std::vector<uint8_t> ents;
			ok = ok && BSPFile::Read(f, entities.fileofs, entities.filelen, ents);
			fclose(f);
			if (!ok)
			{
				return false;
			}
			Crc crc;
			crc.Add(header.raw, sizeof(header.raw));
			crc.Add(&size, sizeof(size));
			crc.Add(ents.data(), ents.size());
			out = crc.Done();
			return true;
		}
	} // namespace

	std::unique_lock<std::mutex> GameThreadLock()
	{
		g_lastFrame = NowMs();
		return std::unique_lock<std::mutex>(g_work);
	}

	void Init(IGameConfig *gc)
	{
		void *sv = nullptr;
		int offset = 0;
		if (gc->GetAddress("sv", &sv) && sv && gc->GetOffset("sv_hibernating", &offset))
		{
			g_hibernating = static_cast<const volatile uint8_t *>(sv) + offset;
		}
		else
		{
			smutils->LogError(myself, "Seam overlay worker disabled: no \"sv\" address or \"sv_hibernating\" offset in gamedata");
		}
		g_mdlcache = static_cast<IMDLCache *>(BSPProps::ModuleInterface("datacache", MDLCACHE_INTERFACE_VERSION));
	}

	int Start(const float values[4], float spacing, const float *origin, bool useCache)
	{
		if (!g_mapReady || !EnsurePoints(spacing)->Count())
		{
			return -1;
		}
		Pass *p = FindPass(values, spacing);
		if (useCache && !p)
		{
			p = &NewPass(values, spacing);
			if (Load(*p))
			{
				g_active = p;
				return 1;
			}
		}
		else if (!useCache && p)
		{
			p->kinds.assign(p->kinds.size(), -1);
			p->saved = false;
		}
		else if (!p)
		{
			p = &NewPass(values, spacing);
		}
		g_active = p;
		Order(*p, origin);
		if (p->Done())
		{
			if (!p->saved)
			{
				Save(*p);
			}
			return 1;
		}
		return 0;
	}

	void Nearest(const float origin[3])
	{
		if (g_active && !g_active->Done())
		{
			Order(*g_active, origin);
		}
	}

	int Step(float seconds)
	{
		if (!g_active)
		{
			return -1;
		}
		auto until = std::chrono::steady_clock::now()
					 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
		while (!g_active->Done() && std::chrono::steady_clock::now() < until)
		{
			RunOne(*g_active);
		}
		if (g_active->Done() && !g_active->saved)
		{
			Save(*g_active);
		}
		return (int)(g_active->order.size() - g_active->next);
	}

	int Kinds(int first, int *out, int maxOut)
	{
		if (!g_active)
		{
			return 0;
		}
		int total = (int)g_active->kinds.size();
		for (int k = 0; k < maxOut && first + k < total; ++k)
		{
			out[k] = g_active->kinds[first + k];
		}
		return total;
	}

	void Queue(const float values[4], float spacing)
	{
		for (const QueueEntry &q : g_queue)
		{
			if (SameValues(q.values, values) && q.spacing == spacing)
			{
				return;
			}
		}
		QueueEntry q {{values[0], values[1], values[2], values[3]}, spacing, false};
		g_queue.push_back(q);
	}

	void OnLoad(const char *extensionPath)
	{
		if (!FileCrc(extensionPath, g_buildKey))
		{
			g_buildKey = 0;
		}
		g_lastFrame = NowMs();
		smutils->AddGameFrameHook(&OnGameFrame);
		playerhelpers->AddClientListener(&g_connect);
		g_stop = false;
		g_thread = std::thread(WorkerMain);
	}

	void OnUnload()
	{
		g_stop = true;
		if (g_thread.joinable())
		{
			g_thread.join();
		}
		playerhelpers->RemoveClientListener(&g_connect);
		smutils->RemoveGameFrameHook(&OnGameFrame);
	}

	void OnMapStart(const char *mapname, const char *bspPath, const char *cacheDir)
	{
		auto lk = GameThreadLock();
		g_map = mapname;
		std::replace(g_map.begin(), g_map.end(), '/', '_');
		std::replace(g_map.begin(), g_map.end(), '\\', '_');
		g_cacheDir = cacheDir;
		MakeDirs(g_cacheDir);
		g_cacheOk = g_buildKey != 0 && BspKey(bspPath, g_bspKey);
		for (QueueEntry &q : g_queue)
		{
			q.done = false;
		}
		g_mapReady = true;
	}

	void OnMapEnd()
	{
		auto lk = GameThreadLock();
		g_mapReady = false;
		g_active = nullptr;
		g_passes.clear();
		g_points.clear();
	}
} // namespace BSPOverlay
