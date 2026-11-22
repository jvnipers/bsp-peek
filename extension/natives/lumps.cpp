#include "natives.h"
#include "bsp_data.h"
#include "bsp_lumps.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// BSP file lump natives
cell_t N_BSPVersion(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::BSPVersion();
}

cell_t N_BSPRevision(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::BSPRevision();
}

cell_t N_LumpInfo(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int ofs = 0, len = 0, ver = 0;
	bool ok = BSPLumps::LumpInfo(params[1], ofs, len, ver);
	cell_t *outOfs, *outLen, *outVer;
	pCtx->LocalToPhysAddr(params[2], &outOfs);
	pCtx->LocalToPhysAddr(params[3], &outLen);
	pCtx->LocalToPhysAddr(params[4], &outVer);
	*outOfs = ofs;
	*outLen = len;
	*outVer = ver;
	return ok ? 1 : 0;
}

cell_t N_HasLighting(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::HasLighting() ? 1 : 0;
}

cell_t N_EntityRawLen(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityRawLen();
}

cell_t N_EntityRawCopy(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[2]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::EntityRawCopy(tmp.data(), maxlen);
	pCtx->StringToLocal(params[1], maxlen, tmp.data());
	return n;
}

cell_t N_EntityCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityCount();
}

cell_t N_EntityClassname(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::EntityClassname(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_EntityOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float origin[3] = {0, 0, 0};
	bool ok = BSPLumps::EntityOrigin(params[1], origin);
	float3_to_cell(pCtx, params[2], origin);
	return ok ? 1 : 0;
}

cell_t N_EntityKeyValue(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[4]);
	if (maxlen <= 0)
	{
		return 0;
	}
	char *key = nullptr;
	pCtx->LocalToString(params[2], &key);
	std::vector<char> tmp(maxlen);
	int nth = params[0] >= 5 ? params[5] : 0; // absent in plugins built against the 4-argument form
	int n = BSPLumps::EntityKeyValue(params[1], key, tmp.data(), maxlen, nth);
	pCtx->StringToLocal(params[3], maxlen, tmp.data());
	return n;
}

cell_t N_EntityModelIndex(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityModelIndex(params[1]);
}

cell_t N_TexInfoCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoCount();
}

cell_t N_TexInfoFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoFlags(params[1]);
}

cell_t N_TexInfoTexData(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoTexData(params[1]);
}

cell_t N_TexDataCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexDataCount();
}

cell_t N_TexDataMaterialName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::TexDataMaterialName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_TexDataReflectivity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float refl[3] = {0, 0, 0};
	bool ok = BSPLumps::TexDataReflectivity(params[1], refl);
	float3_to_cell(pCtx, params[2], refl);
	return ok ? 1 : 0;
}

cell_t N_FaceVertex(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::FaceVertex(params[1], params[2], v);
	float3_to_cell(pCtx, params[3], v);
	return ok ? 1 : 0;
}

cell_t N_FaceCentroid(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::FaceCentroid(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_FaceMaterialName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::FaceMaterialName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_LeafVisible(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	int ca = BSPData::LeafCluster(params[1]);
	int cb = BSPData::LeafCluster(params[2]);
	if (ca < 0 || cb < 0)
	{
		return 0;
	}
	return BSPLumps::ClusterVisible(ca, cb) ? 1 : 0;
}

cell_t N_NearestCubemap(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return BSPLumps::NearestCubemap(pos);
}

cell_t N_FindEntityByKeyValue(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	char *key = nullptr, *value = nullptr;
	pCtx->LocalToString(params[1], &key);
	pCtx->LocalToString(params[2], &value);
	return BSPLumps::FindEntityByKeyValue(key, value, params[3]);
}

cell_t N_VisClusterCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::VisClusterCount();
}

cell_t N_ClusterVisible(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::ClusterVisible(params[1], params[2]) ? 1 : 0;
}

cell_t N_CubemapCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::CubemapCount();
}

cell_t N_CubemapOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::CubemapOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_CubemapSize(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::CubemapSize(params[1]);
}

cell_t N_EdgeCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EdgeCount();
}

cell_t N_EdgeVertices(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int v0 = -1, v1 = -1;
	bool ok = BSPLumps::EdgeVertices(params[1], v0, v1);
	cell_t *outV0, *outV1;
	pCtx->LocalToPhysAddr(params[2], &outV0);
	pCtx->LocalToPhysAddr(params[3], &outV1);
	*outV0 = v0;
	*outV1 = v1;
	return ok ? 1 : 0;
}

cell_t N_SurfedgeCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::SurfedgeCount();
}

cell_t N_Surfedge(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::Surfedge(params[1]);
}

cell_t N_SurfedgeVertex(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::SurfedgeVertex(params[1]);
}

cell_t N_VertexCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::VertexCount();
}

cell_t N_VertexPos(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::VertexPos(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_FaceCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceCount();
}

cell_t N_FacePlaneNum(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FacePlaneNum(params[1]);
}

cell_t N_FaceFirstEdge(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceFirstEdge(params[1]);
}

cell_t N_FaceNumEdges(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceNumEdges(params[1]);
}

cell_t N_FaceTexInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceTexInfo(params[1]);
}

cell_t N_FaceDispInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceDispInfo(params[1]);
}

cell_t N_FaceArea(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::FaceArea(params[1]));
}

cell_t N_FaceLightStyles(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	uint8_t styles[4] = {255, 255, 255, 255};
	bool ok = BSPLumps::FaceLightStyles(params[1], styles);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[2], &out);
	for (int i = 0; i < 4; ++i)
	{
		out[i] = styles[i];
	}
	return ok ? 1 : 0;
}

cell_t N_FaceOrigFace(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceOrigFace(params[1]);
}

cell_t N_FaceLightOfs(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceLightOfs(params[1]);
}

cell_t N_LeafFacesCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::LeafFacesCount();
}

cell_t N_LeafFaces(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	cell_t *buf;
	pCtx->LocalToPhysAddr(params[2], &buf);
	int maxOut = out_len(params[3]);
	if (maxOut <= 0)
	{
		return 0;
	}
	std::vector<int> tmp(maxOut);
	int n = BSPLumps::LeafFaces(params[1], tmp.data(), maxOut);
	for (int i = 0; i < n; ++i)
	{
		buf[i] = tmp[i];
	}
	return n;
}

cell_t N_WorldlightCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightCount();
}

cell_t N_WorldlightOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightIntensity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightIntensity(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightNormal(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightNormal(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightType(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightType(params[1]);
}

cell_t N_WorldlightStyle(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightStyle(params[1]);
}

cell_t N_WorldlightCluster(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightCluster(params[1]);
}

cell_t N_WorldlightShadowCastOffset(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightShadowCastOffset(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightStopDot(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightStopDot(params[1]));
}

cell_t N_WorldlightStopDot2(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightStopDot2(params[1]));
}

cell_t N_WorldlightExponent(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightExponent(params[1]));
}

cell_t N_WorldlightRadius(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightRadius(params[1]));
}

cell_t N_WorldlightConstantAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightConstantAttn(params[1]));
}

cell_t N_WorldlightLinearAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightLinearAttn(params[1]));
}

cell_t N_WorldlightQuadraticAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightQuadraticAttn(params[1]));
}

cell_t N_WorldlightFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightFlags(params[1]);
}

cell_t N_WorldlightTexInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightTexInfo(params[1]);
}

cell_t N_WorldlightOwner(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightOwner(params[1]);
}

// Static props (sprp game lump)
cell_t N_StaticPropCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropCount();
}

cell_t N_StaticPropVersion(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropVersion();
}

cell_t N_StaticPropOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropAngles(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropAngles(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropSolid(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropSolid(params[1]);
}

cell_t N_StaticPropFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropFlags(params[1]);
}

cell_t N_StaticPropModelName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::StaticPropModelName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_StaticPropSkin(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropSkin(params[1]);
}

cell_t N_StaticPropFadeDist(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float mn = 0.0f, mx = 0.0f;
	bool ok = BSPLumps::StaticPropFadeDist(params[1], mn, mx);
	cell_t *pMin, *pMax;
	pCtx->LocalToPhysAddr(params[2], &pMin);
	pCtx->LocalToPhysAddr(params[3], &pMax);
	*pMin = sp_ftoc(mn);
	*pMax = sp_ftoc(mx);
	return ok ? 1 : 0;
}

cell_t N_StaticPropForcedFadeScale(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::StaticPropForcedFadeScale(params[1]));
}

cell_t N_StaticPropLightingOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropLightingOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropFlagsEx(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropFlagsEx(params[1]);
}

cell_t N_StaticPropLeaves(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxOut = out_len(params[3]);
	if (maxOut <= 0)
	{
		return 0;
	}
	cell_t *out;
	pCtx->LocalToPhysAddr(params[2], &out);
	std::vector<int> tmp(maxOut);
	int n = BSPLumps::StaticPropLeaves(params[1], tmp.data(), maxOut);
	for (int i = 0; i < n; ++i)
	{
		out[i] = tmp[i];
	}
	return n;
}

cell_t N_NearestStaticProp(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return BSPLumps::NearestStaticProp(pos, sp_ctof(params[2]));
}

// Leaf water data
cell_t N_LeafWaterCount(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::LeafWaterCount();
}

cell_t N_LeafWaterData(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float surfaceZ, minZ;
	int texinfo;
	bool ok = BSPLumps::LeafWaterData(params[1], surfaceZ, minZ, texinfo);
	cell_t *pSurf, *pMin, *pTex;
	pCtx->LocalToPhysAddr(params[2], &pSurf);
	pCtx->LocalToPhysAddr(params[3], &pMin);
	pCtx->LocalToPhysAddr(params[4], &pTex);
	*pSurf = sp_ftoc(surfaceZ);
	*pMin = sp_ftoc(minZ);
	*pTex = texinfo;
	return ok ? 1 : 0;
}

cell_t N_WaterSurfaceZAt(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return sp_ftoc(BSPLumps::WaterSurfaceZAt(pos));
}

// Brush-entity / trigger helpers (composition over entity + cmodel data)

// World AABB of a brush entity: cmodel (model-local) bounds + entity origin.
cell_t N_EntityBrushBounds(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int modelIdx = BSPLumps::EntityModelIndex(params[1]);
	float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
	bool ok = modelIdx >= 0 && BSPData::CModelBounds(modelIdx, mins, maxs);
	if (ok)
	{
		float org[3] = {0, 0, 0};
		BSPLumps::EntityOrigin(params[1], org); // 0 if no "origin" key
		for (int i = 0; i < 3; ++i)
		{
			// Undo the engine's 1-unit cmodel padding.
			mins[i] += org[i] + 1.0f;
			maxs[i] += org[i] - 1.0f;
		}
	}
	float3_to_cell(pCtx, params[2], mins);
	float3_to_cell(pCtx, params[3], maxs);
	return ok ? 1 : 0;
}

// trigger_push applied velocity = AngleVectors(pushdir).forward * speed.
cell_t N_TriggerPushVelocity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float vel[3] = {0, 0, 0};
	char cls[64];
	BSPLumps::EntityClassname(params[1], cls, sizeof(cls));
	if (std::strcmp(cls, "trigger_push") != 0)
	{
		float3_to_cell(pCtx, params[2], vel);
		return 0;
	}
	char buf[64];
	float pitch = 0, yaw = 0, roll = 0;
	if (BSPLumps::EntityKeyValue(params[1], "pushdir", buf, sizeof(buf)) > 0)
	{
		std::sscanf(buf, "%f %f %f", &pitch, &yaw, &roll);
	}
	float speed = 0.0f;
	if (BSPLumps::EntityKeyValue(params[1], "speed", buf, sizeof(buf)) > 0)
	{
		speed = (float)std::atof(buf);
	}
	if (speed == 0.0f)
	{
		speed = 100.0f; // CTriggerPush::Spawn
	}
	// AngleVectors forward (pitch, yaw): matches the engine's push direction.
	const float d2r = 3.14159265358979323846f / 180.0f;
	float sp = std::sin(pitch * d2r), cp = std::cos(pitch * d2r);
	float sy = std::sin(yaw * d2r), cy = std::cos(yaw * d2r);
	vel[0] = cp * cy * speed;
	vel[1] = cp * sy * speed;
	vel[2] = -sp * speed;
	float3_to_cell(pCtx, params[2], vel);
	return 1;
}

// Origin of the entity targeted by entIdx's "target" key (teleport dest etc.).
cell_t N_EntityTargetOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float origin[3] = {0, 0, 0};
	char target[128];
	bool ok = false;
	if (BSPLumps::EntityKeyValue(params[1], "target", target, sizeof(target)) > 0)
	{
		int dest = BSPLumps::FindEntityByName(target);
		if (dest >= 0)
		{
			ok = BSPLumps::EntityOrigin(dest, origin);
		}
	}
	float3_to_cell(pCtx, params[2], origin);
	return ok ? 1 : 0;
}

extern const sp_nativeinfo_t g_LumpNatives[] = {
	// BSP file lump natives
	{"BSP_BSPVersion", N_BSPVersion},
	{"BSP_BSPRevision", N_BSPRevision},
	{"BSP_LumpInfo", N_LumpInfo},
	{"BSP_HasLighting", N_HasLighting},

	{"BSP_EntityRawLen", N_EntityRawLen},
	{"BSP_EntityRawCopy", N_EntityRawCopy},
	{"BSP_EntityCount", N_EntityCount},
	{"BSP_EntityClassname", N_EntityClassname},
	{"BSP_EntityOrigin", N_EntityOrigin},
	{"BSP_EntityKeyValue", N_EntityKeyValue},
	{"BSP_EntityModelIndex", N_EntityModelIndex},
	{"BSP_FindEntityByKeyValue", N_FindEntityByKeyValue},

	{"BSP_TexInfoCount", N_TexInfoCount},
	{"BSP_TexInfoFlags", N_TexInfoFlags},
	{"BSP_TexInfoTexData", N_TexInfoTexData},
	{"BSP_TexDataCount", N_TexDataCount},
	{"BSP_TexDataMaterialName", N_TexDataMaterialName},
	{"BSP_TexDataReflectivity", N_TexDataReflectivity},

	{"BSP_VisClusterCount", N_VisClusterCount},
	{"BSP_ClusterVisible", N_ClusterVisible},
	{"BSP_LeafVisible", N_LeafVisible},

	{"BSP_CubemapCount", N_CubemapCount},
	{"BSP_CubemapOrigin", N_CubemapOrigin},
	{"BSP_CubemapSize", N_CubemapSize},
	{"BSP_NearestCubemap", N_NearestCubemap},

	{"BSP_EdgeCount", N_EdgeCount},
	{"BSP_EdgeVertices", N_EdgeVertices},
	{"BSP_SurfedgeCount", N_SurfedgeCount},
	{"BSP_Surfedge", N_Surfedge},
	{"BSP_SurfedgeVertex", N_SurfedgeVertex},

	{"BSP_VertexCount", N_VertexCount},
	{"BSP_VertexPos", N_VertexPos},

	{"BSP_FaceVertex", N_FaceVertex},
	{"BSP_FaceCentroid", N_FaceCentroid},
	{"BSP_FaceMaterialName", N_FaceMaterialName},

	{"BSP_FaceCount", N_FaceCount},
	{"BSP_FacePlaneNum", N_FacePlaneNum},
	{"BSP_FaceFirstEdge", N_FaceFirstEdge},
	{"BSP_FaceNumEdges", N_FaceNumEdges},
	{"BSP_FaceTexInfo", N_FaceTexInfo},
	{"BSP_FaceDispInfo", N_FaceDispInfo},
	{"BSP_FaceArea", N_FaceArea},
	{"BSP_FaceLightStyles", N_FaceLightStyles},
	{"BSP_FaceOrigFace", N_FaceOrigFace},
	{"BSP_FaceLightOfs", N_FaceLightOfs},

	{"BSP_LeafFacesCount", N_LeafFacesCount},
	{"BSP_LeafFaces", N_LeafFaces},

	{"BSP_WorldlightCount", N_WorldlightCount},
	{"BSP_WorldlightOrigin", N_WorldlightOrigin},
	{"BSP_WorldlightIntensity", N_WorldlightIntensity},
	{"BSP_WorldlightNormal", N_WorldlightNormal},
	{"BSP_WorldlightType", N_WorldlightType},
	{"BSP_WorldlightStyle", N_WorldlightStyle},
	{"BSP_WorldlightCluster", N_WorldlightCluster},
	{"BSP_WorldlightShadowCastOffset", N_WorldlightShadowCastOffset},
	{"BSP_WorldlightStopDot", N_WorldlightStopDot},
	{"BSP_WorldlightStopDot2", N_WorldlightStopDot2},
	{"BSP_WorldlightExponent", N_WorldlightExponent},
	{"BSP_WorldlightRadius", N_WorldlightRadius},
	{"BSP_WorldlightConstantAttn", N_WorldlightConstantAttn},
	{"BSP_WorldlightLinearAttn", N_WorldlightLinearAttn},
	{"BSP_WorldlightQuadraticAttn", N_WorldlightQuadraticAttn},
	{"BSP_WorldlightFlags", N_WorldlightFlags},
	{"BSP_WorldlightTexInfo", N_WorldlightTexInfo},
	{"BSP_WorldlightOwner", N_WorldlightOwner},

	{"BSP_NumStaticProps", N_StaticPropCount},
	{"BSP_StaticPropVersion", N_StaticPropVersion},
	{"BSP_StaticPropOrigin", N_StaticPropOrigin},
	{"BSP_StaticPropAngles", N_StaticPropAngles},
	{"BSP_StaticPropSolid", N_StaticPropSolid},
	{"BSP_StaticPropFlags", N_StaticPropFlags},
	{"BSP_StaticPropModelName", N_StaticPropModelName},
	{"BSP_StaticPropSkin", N_StaticPropSkin},
	{"BSP_StaticPropFadeDist", N_StaticPropFadeDist},
	{"BSP_StaticPropForcedFadeScale", N_StaticPropForcedFadeScale},
	{"BSP_StaticPropLightingOrigin", N_StaticPropLightingOrigin},
	{"BSP_StaticPropFlagsEx", N_StaticPropFlagsEx},
	{"BSP_StaticPropLeaves", N_StaticPropLeaves},
	{"BSP_NearestStaticProp", N_NearestStaticProp},

	// Leaf water data
	{"BSP_LeafWaterCount", N_LeafWaterCount},
	{"BSP_LeafWaterData", N_LeafWaterData},
	{"BSP_WaterSurfaceZAt", N_WaterSurfaceZAt},

	// Brush-entity / trigger / ladder helpers
	{"BSP_EntityBrushBounds", N_EntityBrushBounds},
	{"BSP_TriggerPushVelocity", N_TriggerPushVelocity},
	{"BSP_EntityTargetOrigin", N_EntityTargetOrigin},

	{nullptr, nullptr},
};
