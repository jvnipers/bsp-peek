#include "bsp_disp.h"
#include "bsp_util.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using BSPUtil::GetKeyInt;
using BSPUtil::kNoHit;
using BSPUtil::ReadF32;
using BSPUtil::ReadI32;
using BSPUtil::ReadPtr;
using BSPUtil::ReadU16;
using BSPUtil::TriHeightZXY;
using BSPUtil::Vec3;

namespace BSPDisp
{

	// ENGINE - reads CDispCollTree array from engine process memory
	namespace engine
	{

		// Address-of-globals.
		// The engine rewrites these slots each map load (CMod_LoadDispInfo), so deref at access time.
		// Caching the value at init is a use-after-free once the old map's tree arrays are freed.
		static int *g_pTreeCount = nullptr;
		static uint8_t **g_ppTreeArrayBase = nullptr;
		static uint8_t **g_ppBoundsArrayBase = nullptr;

		inline uint8_t *TreeArrayBase()
		{
			return g_ppTreeArrayBase ? *g_ppTreeArrayBase : nullptr;
		}

		inline uint8_t *BoundsArrayBase()
		{
			return g_ppBoundsArrayBase ? *g_ppBoundsArrayBase : nullptr;
		}

		static int SZ_DISPCOLL_TREE = 268;
		static int OFF_TREE_MINS = 4;
		static int OFF_TREE_MAXS = 20;
		static int OFF_TREE_POWER = 40;
		static int OFF_TREE_VERTS_PTR = 120;
		static int OFF_TREE_VERTS_CNT = 132;
		static int OFF_TREE_TRIS_PTR = 140;
		static int OFF_TREE_TRIS_CNT = 152;

		static int OFF_TREE_CONTENTS = 32;   // m_nContents (0x20)
		static int OFF_TREE_SURFPROPS = 108; // m_nSurfaceProps[4] (0x6C)

		static int SZ_DISPCOLL_TRI = 24;
		static int OFF_TRI_INDICES = 0;
		static int OFF_TRI_NORMAL = 8; // Vector m_vecNormal within CDispCollTri

		static int SZ_VERT = 12;

		inline const uint8_t *TreePtr(int idx)
		{
			uint8_t *base = TreeArrayBase();
			if (!base || SZ_DISPCOLL_TREE <= 0)
			{
				return nullptr;
			}
			return base + (size_t)idx * SZ_DISPCOLL_TREE;
		}

		// True if the tree has plausible struct fields. Guards against stale memory.
		inline bool IsTreeSane(const uint8_t *tree)
		{
			if (!tree)
			{
				return false;
			}
			int power = ReadI32(tree, OFF_TREE_POWER);
			if (power < 2 || power > 4)
			{
				return false;
			}
			int vertsCnt = ReadI32(tree, OFF_TREE_VERTS_CNT);
			int trisCnt = ReadI32(tree, OFF_TREE_TRIS_CNT);
			// power=2 -> 25 verts, 32 tris ; power=3 -> 81/128 ; power=4 -> 289/512
			// Loose check: count must be > 0 and < a sane cap.
			if (vertsCnt <= 0 || vertsCnt > 512)
			{
				return false;
			}
			if (trisCnt <= 0 || trisCnt > 1024)
			{
				return false;
			}
			uintptr_t vp = reinterpret_cast<uintptr_t>(ReadPtr(tree, OFF_TREE_VERTS_PTR));
			uintptr_t tp = reinterpret_cast<uintptr_t>(ReadPtr(tree, OFF_TREE_TRIS_PTR));
			if (vp < 0x10000 || vp >= 0xF0000000)
			{
				return false;
			}
			if (tp < 0x10000 || tp >= 0xF0000000)
			{
				return false;
			}
			return true;
		}

		// Read a tree's AABB (mins/maxs, 3 floats each) from its struct fields.
		inline void ReadTreeAABB(const uint8_t *tree, float mn[3], float mx[3])
		{
			mn[0] = ReadF32(tree, OFF_TREE_MINS + 0);
			mn[1] = ReadF32(tree, OFF_TREE_MINS + 4);
			mn[2] = ReadF32(tree, OFF_TREE_MINS + 8);
			mx[0] = ReadF32(tree, OFF_TREE_MAXS + 0);
			mx[1] = ReadF32(tree, OFF_TREE_MAXS + 4);
			mx[2] = ReadF32(tree, OFF_TREE_MAXS + 8);
		}

		// Invoke fn(triRec, v0, v1, v2) for every triangle whose indices are in range.
		template<typename F>
		inline void ForEachTreeTriangle(const uint8_t *tree, F &&fn)
		{
			const uint8_t *vertsPtr = (const uint8_t *)ReadPtr(tree, OFF_TREE_VERTS_PTR);
			int vertsCnt = ReadI32(tree, OFF_TREE_VERTS_CNT);
			const uint8_t *trisPtr = (const uint8_t *)ReadPtr(tree, OFF_TREE_TRIS_PTR);
			int trisCnt = ReadI32(tree, OFF_TREE_TRIS_CNT);
			for (int t = 0; t < trisCnt; t++)
			{
				const uint8_t *triRec = trisPtr + (size_t)t * SZ_DISPCOLL_TRI;
				uint16_t i0 = triRec[OFF_TRI_INDICES + 0];
				uint16_t i1 = triRec[OFF_TRI_INDICES + 2];
				uint16_t i2 = triRec[OFF_TRI_INDICES + 4];
				if (i0 >= vertsCnt || i1 >= vertsCnt || i2 >= vertsCnt)
				{
					continue;
				}
				Vec3 v0, v1, v2;
				std::memcpy(&v0, vertsPtr + (size_t)i0 * SZ_VERT, sizeof(Vec3));
				std::memcpy(&v1, vertsPtr + (size_t)i1 * SZ_VERT, sizeof(Vec3));
				std::memcpy(&v2, vertsPtr + (size_t)i2 * SZ_VERT, sizeof(Vec3));
				fn(triRec, v0, v1, v2);
			}
		}

	} // namespace engine

	// DISK - parses BSP file lumps into triangle meshes
	namespace disk
	{

		constexpr int kIdent = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'V');
		constexpr int kHeaderLumps = 64;

		constexpr int LUMP_VERTEXES = 3;
		constexpr int LUMP_FACES = 7;
		constexpr int LUMP_EDGES = 12;
		constexpr int LUMP_SURFEDGES = 13;
		constexpr int LUMP_DISPINFO = 26;
		constexpr int LUMP_DISP_VERTS = 33;

		constexpr int kSizeofVertex = 12;
		constexpr int kSizeofEdge = 4;
		constexpr int kSizeofSurfedge = 4;
		constexpr int kSizeofFace = 56;
		constexpr int kSizeofDispInfo = 176;
		constexpr int kSizeofDispVert = 20;

		constexpr int kFaceOfs_firstedge = 4;
		constexpr int kFaceOfs_numedges = 8;
		constexpr int kFaceOfs_dispinfo = 12;

		constexpr int kDispOfs_startPos = 0;
		constexpr int kDispOfs_dispVertStart = 12;
		constexpr int kDispOfs_power = 20;
		constexpr int kDispOfs_mapFace = 36;

		struct Triangle
		{
			Vec3 v0, v1, v2;
			float minX, maxX, minY, maxY;
		};

		struct Displacement
		{
			float bboxMins[3];
			float bboxMaxs[3];
			std::vector<Triangle> tris;
			float corner0[3], corner1[3], corner2[3], corner3[3];
			int sourcePower;
			int sourceMapFace;
		};

		static std::vector<Displacement> g_disps;
		static char g_loadedMap[128] = {0};

		bool ReadLump(FILE *f, const uint8_t *header, int lump, std::vector<uint8_t> &out)
		{
			int ofs = 8 + lump * 16;
			int32_t fileofs, filelen;
			std::memcpy(&fileofs, header + ofs + 0, 4);
			std::memcpy(&filelen, header + ofs + 4, 4);
			if (filelen <= 0 || fileofs < 0)
			{
				out.clear();
				return true;
			}
			out.resize(filelen);
			if (std::fseek(f, fileofs, SEEK_SET) != 0)
			{
				return false;
			}
			if (std::fread(out.data(), 1, filelen, f) != (size_t)filelen)
			{
				return false;
			}
			return true;
		}

		inline Vec3 ReadVec3(const uint8_t *p)
		{
			Vec3 v;
			std::memcpy(&v.x, p + 0, 4);
			std::memcpy(&v.y, p + 4, 4);
			std::memcpy(&v.z, p + 8, 4);
			return v;
		}

		bool GetBaseQuadCorners(const uint8_t *facesLump, size_t facesLen, const int32_t *surfedges, size_t numSurfedges, const uint16_t *edges,
								size_t numEdges, const Vec3 *vertexes, size_t numVertexes, int faceIdx, const Vec3 &startPos, Vec3 outCorners[4])
		{
			if (faceIdx < 0 || (size_t)(faceIdx + 1) * kSizeofFace > facesLen)
			{
				return false;
			}
			const uint8_t *face = facesLump + (size_t)faceIdx * kSizeofFace;
			int32_t firstedge;
			int16_t numedges;
			std::memcpy(&firstedge, face + kFaceOfs_firstedge, 4);
			std::memcpy(&numedges, face + kFaceOfs_numedges, 2);
			if (numedges != 4)
			{
				return false;
			}
			if (firstedge < 0 || (size_t)(firstedge + numedges) > numSurfedges)
			{
				return false;
			}

			Vec3 quad[4];
			for (int i = 0; i < 4; i++)
			{
				int32_t se = surfedges[firstedge + i];
				uint32_t eIdx = (uint32_t)(se < 0 ? -se : se);
				if ((size_t)eIdx >= numEdges)
				{
					return false;
				}
				uint16_t vIdx = edges[eIdx * 2 + (se < 0 ? 1 : 0)];
				if ((size_t)vIdx >= numVertexes)
				{
					return false;
				}
				quad[i] = vertexes[vIdx];
			}

			int best = 0;
			float bestDistSq = 1e30f;
			for (int i = 0; i < 4; i++)
			{
				float dx = quad[i].x - startPos.x;
				float dy = quad[i].y - startPos.y;
				float dz = quad[i].z - startPos.z;
				float d = dx * dx + dy * dy + dz * dz;
				if (d < bestDistSq)
				{
					bestDistSq = d;
					best = i;
				}
			}
			for (int i = 0; i < 4; i++)
			{
				outCorners[i] = quad[(best + i) & 3];
			}
			return true;
		}

		inline Vec3 BaseAt(const Vec3 corners[4], float u, float v)
		{
			float mu = 1.0f - u, mv = 1.0f - v;
			return {
				mu * mv * corners[0].x + u * mv * corners[1].x + u * v * corners[2].x + mu * v * corners[3].x,
				mu * mv * corners[0].y + u * mv * corners[1].y + u * v * corners[2].y + mu * v * corners[3].y,
				mu * mv * corners[0].z + u * mv * corners[1].z + u * v * corners[2].z + mu * v * corners[3].z,
			};
		}

		void BuildDispMesh(const Vec3 corners[4], int power, const uint8_t *dispVertLump, size_t dispVertLen, int dispVertStart, Displacement &out)
		{
			int side = (1 << power) + 1;
			int numVerts = side * side;
			if (dispVertStart < 0 || (size_t)(dispVertStart + numVerts) * kSizeofDispVert > dispVertLen)
			{
				return;
			}

			std::vector<Vec3> grid(numVerts);
			for (int row = 0; row < side; row++)
			{
				for (int col = 0; col < side; col++)
				{
					float u = (float)col / (float)(side - 1);
					float v = (float)row / (float)(side - 1);
					Vec3 base = BaseAt(corners, u, v);
					int dvIdx = dispVertStart + row * side + col;
					const uint8_t *dv = dispVertLump + (size_t)dvIdx * kSizeofDispVert;
					Vec3 dir = ReadVec3(dv);
					float dist;
					std::memcpy(&dist, dv + 12, 4);
					grid[row * side + col] = {
						base.x + dir.x * dist,
						base.y + dir.y * dist,
						base.z + dir.z * dist,
					};
				}
			}

			int cells = side - 1;
			out.tris.reserve((size_t)cells * cells * 2);
			out.bboxMins[0] = out.bboxMins[1] = out.bboxMins[2] = 1e30f;
			out.bboxMaxs[0] = out.bboxMaxs[1] = out.bboxMaxs[2] = -1e30f;

			auto pushTri = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c)
			{
				Triangle t;
				t.v0 = a;
				t.v1 = b;
				t.v2 = c;
				t.minX = std::fmin(a.x, std::fmin(b.x, c.x));
				t.maxX = std::fmax(a.x, std::fmax(b.x, c.x));
				t.minY = std::fmin(a.y, std::fmin(b.y, c.y));
				t.maxY = std::fmax(a.y, std::fmax(b.y, c.y));
				out.tris.push_back(t);
				for (const Vec3 *v : {&a, &b, &c})
				{
					if (v->x < out.bboxMins[0])
					{
						out.bboxMins[0] = v->x;
					}
					if (v->y < out.bboxMins[1])
					{
						out.bboxMins[1] = v->y;
					}
					if (v->z < out.bboxMins[2])
					{
						out.bboxMins[2] = v->z;
					}
					if (v->x > out.bboxMaxs[0])
					{
						out.bboxMaxs[0] = v->x;
					}
					if (v->y > out.bboxMaxs[1])
					{
						out.bboxMaxs[1] = v->y;
					}
					if (v->z > out.bboxMaxs[2])
					{
						out.bboxMaxs[2] = v->z;
					}
				}
			};

			for (int r = 0; r < cells; r++)
			{
				for (int c = 0; c < cells; c++)
				{
					int ndx = r * side + c;
					const Vec3 &v00 = grid[ndx];
					const Vec3 &v01 = grid[ndx + 1];
					const Vec3 &v10 = grid[ndx + side];
					const Vec3 &v11 = grid[ndx + side + 1];
					bool bOdd = (ndx % 2) == 1;
					if (bOdd)
					{
						pushTri(v00, v10, v01);
						pushTri(v01, v10, v11);
					}
					else
					{
						pushTri(v00, v10, v11);
						pushTri(v00, v11, v01);
					}
				}
			}
		}

		inline float TriHeightZ(const Triangle &t, float x, float y)
		{
			if (x < t.minX || x > t.maxX || y < t.minY || y > t.maxY)
			{
				return NAN;
			}
			return TriHeightZXY(t.v0, t.v1, t.v2, x, y);
		}

	} // namespace disk

	// API - lifecycle
	bool InitEngine(IGameConfig *gc, char *err, size_t errLen)
	{
		void *addr = nullptr;

		if (!gc->GetAddress("g_DispCollTreeCount", &addr) || !addr)
		{
			std::snprintf(err, errLen, "gamedata Address 'g_DispCollTreeCount' did not resolve");
			return false;
		}
		engine::g_pTreeCount = reinterpret_cast<int *>(addr);

		if (!gc->GetAddress("g_pDispCollTrees", &addr) || !addr)
		{
			std::snprintf(err, errLen, "gamedata Address 'g_pDispCollTrees' did not resolve");
			return false;
		}
		engine::g_ppTreeArrayBase = reinterpret_cast<uint8_t **>(addr);

		if (gc->GetAddress("g_pDispBounds", &addr) && addr)
		{
			engine::g_ppBoundsArrayBase = reinterpret_cast<uint8_t **>(addr);
		}

		engine::SZ_DISPCOLL_TREE = GetKeyInt(gc, "dispcoll_tree_sizeof", 268);
		engine::OFF_TREE_MINS = GetKeyInt(gc, "dispcoll_tree_mins", 4);
		engine::OFF_TREE_MAXS = GetKeyInt(gc, "dispcoll_tree_maxs", 20);
		engine::OFF_TREE_POWER = GetKeyInt(gc, "dispcoll_tree_power", 40);
		engine::OFF_TREE_VERTS_PTR = GetKeyInt(gc, "dispcoll_tree_verts_ptr", 120);
		engine::OFF_TREE_VERTS_CNT = GetKeyInt(gc, "dispcoll_tree_verts_cnt", 132);
		engine::OFF_TREE_TRIS_PTR = GetKeyInt(gc, "dispcoll_tree_tris_ptr", 140);
		engine::OFF_TREE_TRIS_CNT = GetKeyInt(gc, "dispcoll_tree_tris_cnt", 152);
		engine::OFF_TREE_CONTENTS = GetKeyInt(gc, "dispcoll_tree_contents", 32);
		engine::OFF_TREE_SURFPROPS = GetKeyInt(gc, "dispcoll_tree_surfprops", 108);
		engine::SZ_DISPCOLL_TRI = GetKeyInt(gc, "dispcoll_tri_sizeof", 24);
		engine::OFF_TRI_INDICES = GetKeyInt(gc, "dispcoll_tri_indices", 0);
		engine::OFF_TRI_NORMAL = GetKeyInt(gc, "dispcoll_tri_normal", 8);
		engine::SZ_VERT = GetKeyInt(gc, "dispcoll_vert_stride", 12);

		if (engine::SZ_DISPCOLL_TREE <= 0 || engine::SZ_DISPCOLL_TRI <= 0)
		{
			std::snprintf(err, errLen, "gamedata Keys for CDispCollTree layout incomplete");
			return false;
		}
		return true;
	}

	void ShutdownEngine()
	{
		engine::g_pTreeCount = nullptr;
		engine::g_ppTreeArrayBase = nullptr;
		engine::g_ppBoundsArrayBase = nullptr;
	}

	void Clear()
	{
		disk::g_disps.clear();
		disk::g_loadedMap[0] = 0;
	}

	bool EnsureLoaded(const char *mapname, const char *bspPath, char *errOut, size_t errLen)
	{
		if (!mapname || !bspPath)
		{
			return false;
		}
		if (disk::g_loadedMap[0] && std::strncmp(disk::g_loadedMap, mapname, sizeof(disk::g_loadedMap)) == 0)
		{
			return true;
		}
		bool ok = LoadFromMap(bspPath, errOut, errLen);
		if (ok)
		{
			std::strncpy(disk::g_loadedMap, mapname, sizeof(disk::g_loadedMap) - 1);
			disk::g_loadedMap[sizeof(disk::g_loadedMap) - 1] = 0;
		}
		return ok;
	}

	bool LoadFromMap(const char *bspPath, char *err, size_t errLen)
	{
		Clear();

		FILE *f = std::fopen(bspPath, "rb");
		if (!f)
		{
			std::snprintf(err, errLen, "cannot open '%s'", bspPath);
			return false;
		}

		constexpr int kHeaderSize = 8 + disk::kHeaderLumps * 16 + 4;
		std::vector<uint8_t> header(kHeaderSize);
		if (std::fread(header.data(), 1, kHeaderSize, f) != kHeaderSize)
		{
			std::snprintf(err, errLen, "short header read");
			std::fclose(f);
			return false;
		}
		int32_t ident;
		std::memcpy(&ident, header.data(), 4);
		if (ident != disk::kIdent)
		{
			std::snprintf(err, errLen, "bad BSP ident 0x%08x", ident);
			std::fclose(f);
			return false;
		}

		std::vector<uint8_t> vertsLump, edgesLump, surfedgesLump, facesLump, dispInfoLump, dispVertsLump;
		if (!disk::ReadLump(f, header.data(), disk::LUMP_VERTEXES, vertsLump) || !disk::ReadLump(f, header.data(), disk::LUMP_EDGES, edgesLump)
			|| !disk::ReadLump(f, header.data(), disk::LUMP_SURFEDGES, surfedgesLump)
			|| !disk::ReadLump(f, header.data(), disk::LUMP_FACES, facesLump) || !disk::ReadLump(f, header.data(), disk::LUMP_DISPINFO, dispInfoLump)
			|| !disk::ReadLump(f, header.data(), disk::LUMP_DISP_VERTS, dispVertsLump))
		{
			std::snprintf(err, errLen, "lump read failed");
			std::fclose(f);
			return false;
		}
		std::fclose(f);

		if (dispInfoLump.empty())
		{
			return true;
		}

		const Vec3 *vertexes = reinterpret_cast<const Vec3 *>(vertsLump.data());
		size_t numVerts = vertsLump.size() / disk::kSizeofVertex;
		const uint16_t *edges = reinterpret_cast<const uint16_t *>(edgesLump.data());
		size_t numEdges = edgesLump.size() / disk::kSizeofEdge;
		const int32_t *surfedges = reinterpret_cast<const int32_t *>(surfedgesLump.data());
		size_t numSurfedges = surfedgesLump.size() / disk::kSizeofSurfedge;

		size_t numDisps = dispInfoLump.size() / disk::kSizeofDispInfo;
		disk::g_disps.reserve(numDisps);

		for (size_t i = 0; i < numDisps; i++)
		{
			const uint8_t *di = dispInfoLump.data() + i * disk::kSizeofDispInfo;
			Vec3 startPos = disk::ReadVec3(di + disk::kDispOfs_startPos);
			int32_t dispVertStart, power;
			uint16_t mapFace;
			std::memcpy(&dispVertStart, di + disk::kDispOfs_dispVertStart, 4);
			std::memcpy(&power, di + disk::kDispOfs_power, 4);
			std::memcpy(&mapFace, di + disk::kDispOfs_mapFace, 2);
			if (power < 1 || power > 4)
			{
				continue;
			}

			Vec3 corners[4];
			if (!disk::GetBaseQuadCorners(facesLump.data(), facesLump.size(), surfedges, numSurfedges, edges, numEdges, vertexes, numVerts,
										  (int)mapFace, startPos, corners))
			{
				continue;
			}

			disk::Displacement d;
			disk::BuildDispMesh(corners, (int)power, dispVertsLump.data(), dispVertsLump.size(), dispVertStart, d);
			if (!d.tris.empty())
			{
				d.sourcePower = (int)power;
				d.sourceMapFace = (int)mapFace;
				std::memcpy(d.corner0, &corners[0], 12);
				std::memcpy(d.corner1, &corners[1], 12);
				std::memcpy(d.corner2, &corners[2], 12);
				std::memcpy(d.corner3, &corners[3], 12);
				disk::g_disps.push_back(std::move(d));
			}
		}
		return true;
	}

	// API - Engine queries
	bool EngineReady()
	{
		return engine::g_pTreeCount && engine::TreeArrayBase() && engine::SZ_DISPCOLL_TREE > 0 && EngineCount() > 0;
	}

	int EngineCount()
	{
		return engine::g_pTreeCount ? *engine::g_pTreeCount : 0;
	}

	float EngineHeightAt(float x, float y)
	{
		int dummy = -1;
		return EngineHeightAtDebug(x, y, dummy);
	}

	float EngineHeightAtDebug(float x, float y, int &outIdx)
	{
		outIdx = -1;
		if (!EngineReady())
		{
			return kNoHit;
		}

		float best = kNoHit;
		int count = EngineCount();

		for (int i = 0; i < count; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!engine::IsTreeSane(tree))
			{
				continue;
			}

			float mn[3], mx[3];
			engine::ReadTreeAABB(tree, mn, mx);
			if (x < mn[0] || x > mx[0] || y < mn[1] || y > mx[1])
			{
				continue;
			}

			engine::ForEachTreeTriangle(tree,
										[&](const uint8_t *, const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
										{
											float triMinX = std::fmin(v0.x, std::fmin(v1.x, v2.x));
											float triMaxX = std::fmax(v0.x, std::fmax(v1.x, v2.x));
											float triMinY = std::fmin(v0.y, std::fmin(v1.y, v2.y));
											float triMaxY = std::fmax(v0.y, std::fmax(v1.y, v2.y));
											if (x < triMinX || x > triMaxX || y < triMinY || y > triMaxY)
											{
												return;
											}
											float z = TriHeightZXY(v0, v1, v2, x, y);
											if (!std::isnan(z) && z > best)
											{
												best = z;
												outIdx = i;
											}
										});
		}
		return best;
	}

	// Closest point on triangle (a,b,c) to p, written into q.
	// Ericson, Real-Time Collision Detection, ClosestPtPointTriangle.
	static void ClosestPtPointTriangle(const float p[3], const Vec3 &a, const Vec3 &b, const Vec3 &c, float q[3])
	{
		float ab[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
		float ac[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
		float ap[3] = {p[0] - a.x, p[1] - a.y, p[2] - a.z};
		float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
		float d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
		if (d1 <= 0.0f && d2 <= 0.0f)
		{
			q[0] = a.x;
			q[1] = a.y;
			q[2] = a.z;
			return;
		}
		float bp[3] = {p[0] - b.x, p[1] - b.y, p[2] - b.z};
		float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
		float d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
		if (d3 >= 0.0f && d4 <= d3)
		{
			q[0] = b.x;
			q[1] = b.y;
			q[2] = b.z;
			return;
		}
		float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
		{
			float v = d1 / (d1 - d3);
			q[0] = a.x + v * ab[0];
			q[1] = a.y + v * ab[1];
			q[2] = a.z + v * ab[2];
			return;
		}
		float cp[3] = {p[0] - c.x, p[1] - c.y, p[2] - c.z};
		float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
		float d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
		if (d6 >= 0.0f && d5 <= d6)
		{
			q[0] = c.x;
			q[1] = c.y;
			q[2] = c.z;
			return;
		}
		float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
		{
			float w = d2 / (d2 - d6);
			q[0] = a.x + w * ac[0];
			q[1] = a.y + w * ac[1];
			q[2] = a.z + w * ac[2];
			return;
		}
		float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
		{
			float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			q[0] = b.x + w * (c.x - b.x);
			q[1] = b.y + w * (c.y - b.y);
			q[2] = b.z + w * (c.z - b.z);
			return;
		}
		float denom = 1.0f / (va + vb + vc);
		float v = vb * denom;
		float w = vc * denom;
		q[0] = a.x + ab[0] * v + ac[0] * w;
		q[1] = a.y + ab[1] * v + ac[1] * w;
		q[2] = a.z + ab[2] * v + ac[2] * w;
	}

	// Min 3D distance from pos to any engine displacement collision triangle within maxDist.
	// Returns kNoHit if no engine disp data or nothing within maxDist.
	float EngineDistToSurface(const float pos[3], float maxDist)
	{
		if (!EngineReady())
		{
			return kNoHit;
		}
		float bestSq = maxDist * maxDist;
		bool found = false;
		int count = EngineCount();
		for (int i = 0; i < count; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!engine::IsTreeSane(tree))
			{
				continue;
			}
			float mn[3], mx[3];
			engine::ReadTreeAABB(tree, mn, mx);
			if (pos[0] < mn[0] - maxDist || pos[0] > mx[0] + maxDist || pos[1] < mn[1] - maxDist || pos[1] > mx[1] + maxDist
				|| pos[2] < mn[2] - maxDist || pos[2] > mx[2] + maxDist)
			{
				continue;
			}
			engine::ForEachTreeTriangle(tree,
										[&](const uint8_t *, const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
										{
											float q[3];
											ClosestPtPointTriangle(pos, v0, v1, v2, q);
											float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
											float dsq = dx * dx + dy * dy + dz * dz;
											if (dsq < bestSq)
											{
												bestSq = dsq;
												found = true;
											}
										});
		}
		return found ? std::sqrt(bestSq) : kNoHit;
	}

	// EngineDistToSurface + the nearest tri's stored normal (CDispCollTri::m_vecNormal) and 3 world-space verts.
	// kNoHit if none in range.
	float EngineNearestTri(const float pos[3], float maxDist, float outNormal[3], float outV0[3], float outV1[3], float outV2[3])
	{
		outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
		outV0[0] = outV0[1] = outV0[2] = 0.0f;
		outV1[0] = outV1[1] = outV1[2] = 0.0f;
		outV2[0] = outV2[1] = outV2[2] = 0.0f;
		if (!EngineReady())
		{
			return kNoHit;
		}
		float bestSq = maxDist * maxDist;
		bool found = false;
		Vec3 bN = {0, 0, 0}, bV0 = {0, 0, 0}, bV1 = {0, 0, 0}, bV2 = {0, 0, 0};
		int count = EngineCount();
		for (int i = 0; i < count; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!engine::IsTreeSane(tree))
			{
				continue;
			}
			float mn[3], mx[3];
			engine::ReadTreeAABB(tree, mn, mx);
			if (pos[0] < mn[0] - maxDist || pos[0] > mx[0] + maxDist || pos[1] < mn[1] - maxDist || pos[1] > mx[1] + maxDist
				|| pos[2] < mn[2] - maxDist || pos[2] > mx[2] + maxDist)
			{
				continue;
			}
			engine::ForEachTreeTriangle(tree,
										[&](const uint8_t *triRec, const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
										{
											float q[3];
											ClosestPtPointTriangle(pos, v0, v1, v2, q);
											float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
											float dsq = dx * dx + dy * dy + dz * dz;
											if (dsq < bestSq)
											{
												bestSq = dsq;
												found = true;
												std::memcpy(&bN, triRec + engine::OFF_TRI_NORMAL, sizeof(Vec3));
												bV0 = v0;
												bV1 = v1;
												bV2 = v2;
											}
										});
		}
		if (!found)
		{
			return kNoHit;
		}
		outNormal[0] = bN.x;
		outNormal[1] = bN.y;
		outNormal[2] = bN.z;
		outV0[0] = bV0.x;
		outV0[1] = bV0.y;
		outV0[2] = bV0.z;
		outV1[0] = bV1.x;
		outV1[1] = bV1.y;
		outV1[2] = bV1.z;
		outV2[0] = bV2.x;
		outV2[1] = bV2.y;
		outV2[2] = bV2.z;
		return std::sqrt(bestSq);
	}

	// Engine disp tree (= displacement face) index nearest to pos within maxDist.
	// -1 if none. Adjacent displacements are distinct trees.
	int EngineTreeIndexAt(const float pos[3], float maxDist)
	{
		if (!EngineReady())
		{
			return -1;
		}
		float bestSq = maxDist * maxDist;
		int bestTree = -1;
		int count = EngineCount();
		for (int i = 0; i < count; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!engine::IsTreeSane(tree))
			{
				continue;
			}
			float mn[3], mx[3];
			engine::ReadTreeAABB(tree, mn, mx);
			if (pos[0] < mn[0] - maxDist || pos[0] > mx[0] + maxDist || pos[1] < mn[1] - maxDist || pos[1] > mx[1] + maxDist
				|| pos[2] < mn[2] - maxDist || pos[2] > mx[2] + maxDist)
			{
				continue;
			}
			engine::ForEachTreeTriangle(tree,
										[&](const uint8_t *, const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
										{
											float q[3];
											ClosestPtPointTriangle(pos, v0, v1, v2, q);
											float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
											float dsq = dx * dx + dy * dy + dz * dz;
											if (dsq < bestSq)
											{
												bestSq = dsq;
												bestTree = i;
											}
										});
		}
		return bestTree;
	}

	int EngineDebugTreeInfo(int idx, char *buf, size_t bufLen)
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return std::snprintf(buf, bufLen, "engine disp tree idx %d unavailable (ready=%d count=%d)", idx, EngineReady() ? 1 : 0, EngineCount());
		}
		const uint8_t *tree = engine::TreePtr(idx);
		Vec3 mn = {0, 0, 0}, mx = {0, 0, 0};
		std::memcpy(&mn, tree + engine::OFF_TREE_MINS, sizeof(Vec3));
		std::memcpy(&mx, tree + engine::OFF_TREE_MAXS, sizeof(Vec3));
		int power = ReadI32(tree, engine::OFF_TREE_POWER);
		int vCnt = ReadI32(tree, engine::OFF_TREE_VERTS_CNT);
		int tCnt = ReadI32(tree, engine::OFF_TREE_TRIS_CNT);
		void *vp = ReadPtr(tree, engine::OFF_TREE_VERTS_PTR);
		void *tp = ReadPtr(tree, engine::OFF_TREE_TRIS_PTR);

		auto dumpHex = [](const void *p, char *out, size_t outLen, int n)
		{
			uintptr_t pi = reinterpret_cast<uintptr_t>(p);
			if (!p || pi <= 0x10000 || pi >= 0xF0000000)
			{
				std::snprintf(out, outLen, "(unmapped)");
				return;
			}
			const uint8_t *b = (const uint8_t *)p;
			int w = 0;
			for (int i = 0; i < n && w + 4 < (int)outLen; i++)
			{
				w += std::snprintf(out + w, outLen - w, "%s%02X", (i && (i % 16 == 0)) ? "|" : (i ? " " : ""), b[i]);
			}
		};
		char triHex[256], vertHex[256];
		dumpHex(tp, triHex, sizeof(triHex), 48);
		dumpHex(vp, vertHex, sizeof(vertHex), 48);
		return std::snprintf(buf, bufLen,
							 "engine[%d] base=%p power=%d verts=%d tris=%d "
							 "bbox=(%.3f,%.3f,%.3f)..(%.3f,%.3f,%.3f) vp=%p tp=%p\n"
							 "    tri0[%s]\n    vp0[%s]",
							 idx, (const void *)tree, power, vCnt, tCnt, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, vp, tp, triHex, vertHex);
	}

	int EngineDiagnoseQuery(float x, float y, char *buf, size_t bufLen)
	{
		if (!EngineReady())
		{
			return std::snprintf(buf, bufLen, "engine not ready");
		}
		int treeCount = EngineCount();
		int treesAABB = 0, firstTree = -1;
		for (int i = 0; i < treeCount; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!tree)
			{
				continue;
			}
			float minX = ReadF32(tree, engine::OFF_TREE_MINS + 0);
			float minY = ReadF32(tree, engine::OFF_TREE_MINS + 4);
			float maxX = ReadF32(tree, engine::OFF_TREE_MAXS + 0);
			float maxY = ReadF32(tree, engine::OFF_TREE_MAXS + 4);
			if (x >= minX && x <= maxX && y >= minY && y <= maxY)
			{
				treesAABB++;
				if (firstTree < 0)
				{
					firstTree = i;
				}
			}
		}
		if (firstTree < 0)
		{
			return std::snprintf(buf, bufLen, "diag (%.3f,%.3f): 0/%d trees AABB-cover point", x, y, treeCount);
		}
		const uint8_t *tree = engine::TreePtr(firstTree);
		const uint8_t *vertsPtr = (const uint8_t *)ReadPtr(tree, engine::OFF_TREE_VERTS_PTR);
		int vertsCnt = ReadI32(tree, engine::OFF_TREE_VERTS_CNT);
		const uint8_t *trisPtr = (const uint8_t *)ReadPtr(tree, engine::OFF_TREE_TRIS_PTR);
		int trisCnt = ReadI32(tree, engine::OFF_TREE_TRIS_CNT);
		int trisBboxPass = 0, trisInvalidIdx = 0, trisDegen = 0, trisBaryFail = 0, trisHit = 0;
		float bestZ = kNoHit, bestWMin = -2.0f;
		for (int t = 0; t < trisCnt; t++)
		{
			const uint8_t *triRec = trisPtr + (size_t)t * engine::SZ_DISPCOLL_TRI;
			uint16_t i0 = triRec[engine::OFF_TRI_INDICES + 0];
			uint16_t i1 = triRec[engine::OFF_TRI_INDICES + 2];
			uint16_t i2 = triRec[engine::OFF_TRI_INDICES + 4];
			if (i0 >= vertsCnt || i1 >= vertsCnt || i2 >= vertsCnt)
			{
				trisInvalidIdx++;
				continue;
			}
			Vec3 v0, v1, v2;
			std::memcpy(&v0, vertsPtr + (size_t)i0 * engine::SZ_VERT, sizeof(Vec3));
			std::memcpy(&v1, vertsPtr + (size_t)i1 * engine::SZ_VERT, sizeof(Vec3));
			std::memcpy(&v2, vertsPtr + (size_t)i2 * engine::SZ_VERT, sizeof(Vec3));
			float triMinX = std::fmin(v0.x, std::fmin(v1.x, v2.x));
			float triMaxX = std::fmax(v0.x, std::fmax(v1.x, v2.x));
			float triMinY = std::fmin(v0.y, std::fmin(v1.y, v2.y));
			float triMaxY = std::fmax(v0.y, std::fmax(v1.y, v2.y));
			if (x < triMinX || x > triMaxX || y < triMinY || y > triMaxY)
			{
				continue;
			}
			trisBboxPass++;
			float denom = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
			if (std::fabs(denom) < 2.0f)
			{
				trisDegen++;
				continue;
			}
			float wa = ((v1.y - v2.y) * (x - v2.x) + (v2.x - v1.x) * (y - v2.y)) / denom;
			float wb = ((v2.y - v0.y) * (x - v2.x) + (v0.x - v2.x) * (y - v2.y)) / denom;
			float wc = 1.0f - wa - wb;
			float wmin = std::fmin(wa, std::fmin(wb, wc));
			if (wmin < -1e-4f)
			{
				trisBaryFail++;
				if (wmin > bestWMin)
				{
					bestWMin = wmin;
				}
				continue;
			}
			trisHit++;
			float z = wa * v0.z + wb * v1.z + wc * v2.z;
			if (z > bestZ)
			{
				bestZ = z;
			}
		}
		return std::snprintf(buf, bufLen,
							 "diag (%.3f,%.3f): AABB-trees=%d, first=%d (verts=%d tris=%d) -- "
							 "bbox_pass=%d invalid_idx=%d degen=%d bary_fail=%d hit=%d "
							 "best_z=%.3f closest_neg_w=%.4f",
							 x, y, treesAABB, firstTree, vertsCnt, trisCnt, trisBboxPass, trisInvalidIdx, trisDegen, trisBaryFail, trisHit, bestZ,
							 bestWMin);
	}

	// API - Disk queries
	int DiskCount()
	{
		return (int)disk::g_disps.size();
	}

	bool DiskGetBounds(int idx, float mins[3], float maxs[3])
	{
		if (idx < 0 || (size_t)idx >= disk::g_disps.size())
		{
			return false;
		}
		const auto &d = disk::g_disps[idx];
		std::memcpy(mins, d.bboxMins, 12);
		std::memcpy(maxs, d.bboxMaxs, 12);
		return true;
	}

	float DiskHeightAt(float x, float y)
	{
		int dummy = -1;
		return DiskHeightAtDebug(x, y, dummy);
	}

	float DiskHeightAtDebug(float x, float y, int &outDispIdx)
	{
		float best = kNoHit;
		outDispIdx = -1;
		for (size_t i = 0; i < disk::g_disps.size(); i++)
		{
			const auto &d = disk::g_disps[i];
			if (x < d.bboxMins[0] || x > d.bboxMaxs[0])
			{
				continue;
			}
			if (y < d.bboxMins[1] || y > d.bboxMaxs[1])
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				float z = disk::TriHeightZ(t, x, y);
				if (!std::isnan(z) && z > best)
				{
					best = z;
					outDispIdx = (int)i;
				}
			}
		}
		return best;
	}

	int DiskDebugDispInfo(int idx, char *buf, size_t bufLen)
	{
		if (idx < 0 || (size_t)idx >= disk::g_disps.size())
		{
			return std::snprintf(buf, bufLen, "disp idx %d out of range (count=%d)", idx, (int)disk::g_disps.size());
		}
		const auto &d = disk::g_disps[idx];
		return std::snprintf(buf, bufLen,
							 "disp[%d] face=%d power=%d tris=%d "
							 "bbox=(%.3f,%.3f,%.3f)..(%.3f,%.3f,%.3f) "
							 "c0=(%.3f,%.3f,%.3f) c1=(%.3f,%.3f,%.3f) "
							 "c2=(%.3f,%.3f,%.3f) c3=(%.3f,%.3f,%.3f)",
							 idx, d.sourceMapFace, d.sourcePower, (int)d.tris.size(), d.bboxMins[0], d.bboxMins[1], d.bboxMins[2], d.bboxMaxs[0],
							 d.bboxMaxs[1], d.bboxMaxs[2], d.corner0[0], d.corner0[1], d.corner0[2], d.corner1[0], d.corner1[1], d.corner1[2],
							 d.corner2[0], d.corner2[1], d.corner2[2], d.corner3[0], d.corner3[1], d.corner3[2]);
	}

	// Engine per-tree accessors
	bool EngineGetBounds(int idx, float mins[3], float maxs[3])
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return false;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return false;
		}
		Vec3 mn, mx;
		std::memcpy(&mn, tree + engine::OFF_TREE_MINS, sizeof(Vec3));
		std::memcpy(&mx, tree + engine::OFF_TREE_MAXS, sizeof(Vec3));
		mins[0] = mn.x;
		mins[1] = mn.y;
		mins[2] = mn.z;
		maxs[0] = mx.x;
		maxs[1] = mx.y;
		maxs[2] = mx.z;
		return true;
	}

	int EngineGetPower(int idx)
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return -1;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return -1;
		}
		return ReadI32(tree, engine::OFF_TREE_POWER);
	}

	int EngineGetContents(int idx)
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return 0;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return 0;
		}
		return ReadI32(tree, engine::OFF_TREE_CONTENTS);
	}

	bool EngineGetSurfaceProps(int idx, int props[4])
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return false;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return false;
		}
		// m_nSurfaceProps[4] is array of shorts.
		for (int i = 0; i < 4; i++)
		{
			int16_t v;
			std::memcpy(&v, tree + engine::OFF_TREE_SURFPROPS + i * 2, 2);
			props[i] = (int)v;
		}
		return true;
	}

	int EngineVertCount(int idx)
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return -1;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return -1;
		}
		return ReadI32(tree, engine::OFF_TREE_VERTS_CNT);
	}

	int EngineTriCount(int idx)
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return -1;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!tree)
		{
			return -1;
		}
		return ReadI32(tree, engine::OFF_TREE_TRIS_CNT);
	}

	bool EngineGetVert(int idx, int vertIdx, float pos[3])
	{
		if (!EngineReady() || idx < 0 || idx >= EngineCount())
		{
			return false;
		}
		const uint8_t *tree = engine::TreePtr(idx);
		if (!engine::IsTreeSane(tree))
		{
			return false;
		}
		const uint8_t *vertsPtr = (const uint8_t *)ReadPtr(tree, engine::OFF_TREE_VERTS_PTR);
		int vertsCnt = ReadI32(tree, engine::OFF_TREE_VERTS_CNT);
		if (vertIdx < 0 || vertIdx >= vertsCnt)
		{
			return false;
		}
		Vec3 v;
		std::memcpy(&v, vertsPtr + (size_t)vertIdx * engine::SZ_VERT, sizeof(Vec3));
		pos[0] = v.x;
		pos[1] = v.y;
		pos[2] = v.z;
		return true;
	}

	// Engine query with normal output
	static float EngineSurfaceNormalAt(float x, float y, float outNormal[3])
	{
		outNormal[0] = 0;
		outNormal[1] = 0;
		outNormal[2] = 1;
		if (!EngineReady())
		{
			return kNoHit;
		}

		float best = kNoHit;
		int count = EngineCount();
		Vec3 bestNormal = {0, 0, 1};

		for (int i = 0; i < count; i++)
		{
			const uint8_t *tree = engine::TreePtr(i);
			if (!engine::IsTreeSane(tree))
			{
				continue;
			}
			float mn[3], mx[3];
			engine::ReadTreeAABB(tree, mn, mx);
			if (x < mn[0] || x > mx[0] || y < mn[1] || y > mx[1])
			{
				continue;
			}

			engine::ForEachTreeTriangle(tree,
										[&](const uint8_t *triRec, const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
										{
											float triMinX = std::fmin(v0.x, std::fmin(v1.x, v2.x));
											float triMaxX = std::fmax(v0.x, std::fmax(v1.x, v2.x));
											float triMinY = std::fmin(v0.y, std::fmin(v1.y, v2.y));
											float triMaxY = std::fmax(v0.y, std::fmax(v1.y, v2.y));
											if (x < triMinX || x > triMaxX || y < triMinY || y > triMaxY)
											{
												return;
											}
											float z = TriHeightZXY(v0, v1, v2, x, y);
											if (!std::isnan(z) && z > best)
											{
												best = z;
												std::memcpy(&bestNormal, triRec + engine::OFF_TRI_NORMAL, sizeof(Vec3));
											}
										});
		}
		if (best > kNoHit)
		{
			outNormal[0] = bestNormal.x;
			outNormal[1] = bestNormal.y;
			outNormal[2] = bestNormal.z;
		}
		return best;
	}

	// API - Unified (engine-first, disk fallback)
	float HeightAt(float x, float y)
	{
		int dummy = -1;
		return HeightAtDebug(x, y, dummy);
	}

	float HeightAtDebug(float x, float y, int &outIdx)
	{
		if (EngineReady())
		{
			float z = EngineHeightAtDebug(x, y, outIdx);
			if (z > kNoHit)
			{
				return z;
			}
		}
		return DiskHeightAtDebug(x, y, outIdx);
	}

	float SurfaceNormalAt(float x, float y, float normal[3])
	{
		if (EngineReady())
		{
			float z = EngineSurfaceNormalAt(x, y, normal);
			if (z > kNoHit)
			{
				return z;
			}
		}
		// Disk fallback: use tri verts to compute normal via cross product.
		normal[0] = 0;
		normal[1] = 0;
		normal[2] = 1;
		float best = kNoHit;
		Vec3 bestN = {0, 0, 1};
		for (size_t i = 0; i < disk::g_disps.size(); i++)
		{
			const auto &d = disk::g_disps[i];
			if (x < d.bboxMins[0] || x > d.bboxMaxs[0])
			{
				continue;
			}
			if (y < d.bboxMins[1] || y > d.bboxMaxs[1])
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				float z = disk::TriHeightZ(t, x, y);
				if (std::isnan(z) || z <= best)
				{
					continue;
				}
				best = z;
				// Compute normal from cross product of edges.
				float ex0 = t.v1.x - t.v0.x, ey0 = t.v1.y - t.v0.y, ez0 = t.v1.z - t.v0.z;
				float ex1 = t.v2.x - t.v0.x, ey1 = t.v2.y - t.v0.y, ez1 = t.v2.z - t.v0.z;
				Vec3 n = {
					ey0 * ez1 - ez0 * ey1,
					ez0 * ex1 - ex0 * ez1,
					ex0 * ey1 - ey0 * ex1,
				};
				float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
				if (len > 1e-6f)
				{
					n.x /= len;
					n.y /= len;
					n.z /= len;
				}
				// Ensure normal points up (disp surface).
				if (n.z < 0)
				{
					n.x = -n.x;
					n.y = -n.y;
					n.z = -n.z;
				}
				bestN = n;
			}
		}
		if (best > kNoHit)
		{
			normal[0] = bestN.x;
			normal[1] = bestN.y;
			normal[2] = bestN.z;
		}
		return best;
	}

	bool IsPointOnDisp(float x, float y)
	{
		int dummy = -1;
		return HeightAtDebug(x, y, dummy) > kNoHit;
	}

	int HeightAtMulti(float x, float y, float *results, int maxResults)
	{
		if (!results || maxResults <= 0)
		{
			return 0;
		}
		int count = 0;

		if (EngineReady())
		{
			int treeCount = EngineCount();
			for (int i = 0; i < treeCount && count < maxResults; i++)
			{
				const uint8_t *tree = engine::TreePtr(i);
				if (!engine::IsTreeSane(tree))
				{
					continue;
				}
				float minX = ReadF32(tree, engine::OFF_TREE_MINS + 0);
				float minY = ReadF32(tree, engine::OFF_TREE_MINS + 4);
				float maxX = ReadF32(tree, engine::OFF_TREE_MAXS + 0);
				float maxY = ReadF32(tree, engine::OFF_TREE_MAXS + 4);
				if (x < minX || x > maxX || y < minY || y > maxY)
				{
					continue;
				}

				const uint8_t *vertsPtr = (const uint8_t *)ReadPtr(tree, engine::OFF_TREE_VERTS_PTR);
				int vertsCnt = ReadI32(tree, engine::OFF_TREE_VERTS_CNT);
				const uint8_t *trisPtr = (const uint8_t *)ReadPtr(tree, engine::OFF_TREE_TRIS_PTR);
				int trisCnt = ReadI32(tree, engine::OFF_TREE_TRIS_CNT);

				for (int t = 0; t < trisCnt && count < maxResults; t++)
				{
					const uint8_t *triRec = trisPtr + (size_t)t * engine::SZ_DISPCOLL_TRI;
					uint16_t i0 = triRec[engine::OFF_TRI_INDICES + 0];
					uint16_t i1 = triRec[engine::OFF_TRI_INDICES + 2];
					uint16_t i2 = triRec[engine::OFF_TRI_INDICES + 4];
					if (i0 >= vertsCnt || i1 >= vertsCnt || i2 >= vertsCnt)
					{
						continue;
					}

					Vec3 v0, v1, v2;
					std::memcpy(&v0, vertsPtr + (size_t)i0 * engine::SZ_VERT, sizeof(Vec3));
					std::memcpy(&v1, vertsPtr + (size_t)i1 * engine::SZ_VERT, sizeof(Vec3));
					std::memcpy(&v2, vertsPtr + (size_t)i2 * engine::SZ_VERT, sizeof(Vec3));

					float triMinX = std::fmin(v0.x, std::fmin(v1.x, v2.x));
					float triMaxX = std::fmax(v0.x, std::fmax(v1.x, v2.x));
					float triMinY = std::fmin(v0.y, std::fmin(v1.y, v2.y));
					float triMaxY = std::fmax(v0.y, std::fmax(v1.y, v2.y));
					if (x < triMinX || x > triMaxX || y < triMinY || y > triMaxY)
					{
						continue;
					}

					float z = TriHeightZXY(v0, v1, v2, x, y);
					if (!std::isnan(z))
					{
						results[count++] = z;
					}
				}
			}
			if (count > 0)
			{
				return count;
			}
		}

		// Disk fallback
		for (size_t i = 0; i < disk::g_disps.size() && count < maxResults; i++)
		{
			const auto &d = disk::g_disps[i];
			if (x < d.bboxMins[0] || x > d.bboxMaxs[0])
			{
				continue;
			}
			if (y < d.bboxMins[1] || y > d.bboxMaxs[1])
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				if (count >= maxResults)
				{
					break;
				}
				float z = disk::TriHeightZ(t, x, y);
				if (!std::isnan(z))
				{
					results[count++] = z;
				}
			}
		}
		return count;
	}

	// Unified 3D nearest-displacement-surface distance, Returns kNoHit if no disp data or nothing within maxDist.
	float DistToSurface(const float pos[3], float maxDist)
	{
		if (EngineReady())
		{
			return EngineDistToSurface(pos, maxDist);
		}

		// Disk fallback: 3D nearest-triangle over parsed disp meshes.
		float bestSq = maxDist * maxDist;
		bool found = false;
		for (size_t i = 0; i < disk::g_disps.size(); i++)
		{
			const auto &d = disk::g_disps[i];
			if (pos[0] < d.bboxMins[0] - maxDist || pos[0] > d.bboxMaxs[0] + maxDist || pos[1] < d.bboxMins[1] - maxDist
				|| pos[1] > d.bboxMaxs[1] + maxDist || pos[2] < d.bboxMins[2] - maxDist || pos[2] > d.bboxMaxs[2] + maxDist)
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				float q[3];
				ClosestPtPointTriangle(pos, t.v0, t.v1, t.v2, q);
				float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
				float dsq = dx * dx + dy * dy + dz * dz;
				if (dsq < bestSq)
				{
					bestSq = dsq;
					found = true;
				}
			}
		}
		return found ? std::sqrt(bestSq) : kNoHit;
	}

	// Unified nearest-disp-triangle. Fills the tri's normal + 3 verts.
	// Disk fallback computes the normal from the verts.
	// kNoHit if nothing within maxDist.
	float DistNearestTri(const float pos[3], float maxDist, float normal[3], float v0[3], float v1[3], float v2[3])
	{
		if (EngineReady())
		{
			return EngineNearestTri(pos, maxDist, normal, v0, v1, v2);
		}

		normal[0] = normal[1] = normal[2] = 0.0f;
		v0[0] = v0[1] = v0[2] = 0.0f;
		v1[0] = v1[1] = v1[2] = 0.0f;
		v2[0] = v2[1] = v2[2] = 0.0f;
		float bestSq = maxDist * maxDist;
		bool found = false;
		Vec3 bV0 = {0, 0, 0}, bV1 = {0, 0, 0}, bV2 = {0, 0, 0};
		for (size_t i = 0; i < disk::g_disps.size(); i++)
		{
			const auto &d = disk::g_disps[i];
			if (pos[0] < d.bboxMins[0] - maxDist || pos[0] > d.bboxMaxs[0] + maxDist || pos[1] < d.bboxMins[1] - maxDist
				|| pos[1] > d.bboxMaxs[1] + maxDist || pos[2] < d.bboxMins[2] - maxDist || pos[2] > d.bboxMaxs[2] + maxDist)
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				float q[3];
				ClosestPtPointTriangle(pos, t.v0, t.v1, t.v2, q);
				float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
				float dsq = dx * dx + dy * dy + dz * dz;
				if (dsq < bestSq)
				{
					bestSq = dsq;
					found = true;
					bV0 = t.v0;
					bV1 = t.v1;
					bV2 = t.v2;
				}
			}
		}
		if (!found)
		{
			return kNoHit;
		}
		// normal = (v1-v0) x (v2-v0), normalized.
		float ax = bV1.x - bV0.x, ay = bV1.y - bV0.y, az = bV1.z - bV0.z;
		float bx = bV2.x - bV0.x, by = bV2.y - bV0.y, bz = bV2.z - bV0.z;
		float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
		float len = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (len > 1e-6f)
		{
			nx /= len;
			ny /= len;
			nz /= len;
		}
		normal[0] = nx;
		normal[1] = ny;
		normal[2] = nz;
		v0[0] = bV0.x;
		v0[1] = bV0.y;
		v0[2] = bV0.z;
		v1[0] = bV1.x;
		v1[1] = bV1.y;
		v1[2] = bV1.z;
		v2[0] = bV2.x;
		v2[1] = bV2.y;
		v2[2] = bV2.z;
		return std::sqrt(bestSq);
	}

	// Unified nearest disp tree/face index. -1 if none.
	// Used to detect the flush boundary between two displacements.
	int TreeIndexAt(const float pos[3], float maxDist)
	{
		if (EngineReady())
		{
			return EngineTreeIndexAt(pos, maxDist);
		}

		float bestSq = maxDist * maxDist;
		int best = -1;
		for (size_t i = 0; i < disk::g_disps.size(); i++)
		{
			const auto &d = disk::g_disps[i];
			if (pos[0] < d.bboxMins[0] - maxDist || pos[0] > d.bboxMaxs[0] + maxDist || pos[1] < d.bboxMins[1] - maxDist
				|| pos[1] > d.bboxMaxs[1] + maxDist || pos[2] < d.bboxMins[2] - maxDist || pos[2] > d.bboxMaxs[2] + maxDist)
			{
				continue;
			}
			for (const auto &t : d.tris)
			{
				float q[3];
				ClosestPtPointTriangle(pos, t.v0, t.v1, t.v2, q);
				float dx = pos[0] - q[0], dy = pos[1] - q[1], dz = pos[2] - q[2];
				float dsq = dx * dx + dy * dy + dz * dz;
				if (dsq < bestSq)
				{
					bestSq = dsq;
					best = (int)i;
				}
			}
		}
		return best;
	}

} // namespace BSPDisp
