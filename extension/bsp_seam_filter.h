#ifndef _INCLUDE_BSPPEEK_BSP_SEAM_FILTER_H_
#define _INCLUDE_BSPPEEK_BSP_SEAM_FILTER_H_

#include "bsp_props.h"

#include "const.h"
#include "mathlib/mathlib.h"
#include "engine/ICollideable.h"
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "iserverunknown.h"

namespace BSPSeam
{
	namespace
	{
		// CTraceFilterSimple with COLLISION_GROUP_PLAYER_MOVEMENT, minus players.
		// Groups: CCSGameRules::ShouldCollide and its base in CS:GO server.dll (CCSGameRules vtable slot 28).
		class MovementFilter : public ITraceFilter
		{
		public:
			bool ShouldHitEntity(IHandleEntity *ent, int) override
			{
				if (BSPProps::StaticPropMgr() && BSPProps::StaticPropMgr()->IsStaticProp(ent))
				{
					return true;
				}
				ICollideable *c = static_cast<IServerUnknown *>(ent)->GetCollideable();
				if (!c || c->GetSolid() == SOLID_NONE || (c->GetSolidFlags() & FSOLID_NOT_SOLID))
				{
					return false;
				}
				switch (c->GetCollisionGroup())
				{
					case COLLISION_GROUP_DEBRIS:
					case COLLISION_GROUP_DEBRIS_TRIGGER:
					case COLLISION_GROUP_INTERACTIVE_DEBRIS:
					case COLLISION_GROUP_PLAYER:
					case COLLISION_GROUP_IN_VEHICLE:
					case COLLISION_GROUP_WEAPON:
					case COLLISION_GROUP_PROJECTILE:
					case COLLISION_GROUP_DOOR_BLOCKER:
					case COLLISION_GROUP_DISSOLVING:
					case COLLISION_GROUP_PUSHAWAY:
					case COLLISION_GROUP_VEHICLE_CLIP:
					case COLLISION_GROUP_DEBRIS_BLOCK_PROJECTILE:
						return false;
					default:
						return true;
				}
			}

			TraceType_t GetTraceType() const override
			{
				return TRACE_EVERYTHING;
			}
		};
	} // namespace
} // namespace BSPSeam

#endif
