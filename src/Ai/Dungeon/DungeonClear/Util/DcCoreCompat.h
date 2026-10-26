/*
 * mod-dungeon-clear — DcCoreCompat.h
 *
 * The seams where the playerbots FORK core and the upstream-shaped core it is
 * being folded back into have no API in common, so DC has to pick one at
 * compile time.
 *
 * mod-playerbots #2765 / #2792 / #2820 (test-staging, Sep 2026) move playerbots
 * off the fork-only core hooks and onto upstream AzerothCore's, and the fork
 * core deletes the fork-only half as it syncs:
 *
 *   fork core (old)                     upstream-shaped core (new)
 *   ------------------------------      --------------------------------------
 *   PlayerbotScript                     gone
 *     OnPlayerbotUpdate(diff)             -> WorldScript::OnUpdate (no detection
 *                                            needed: WorldScript is on both)
 *     OnPlayerbotPacketSent(p, pkt)       -> ServerScript::OnPacketSent(s, pkt)
 *   WorldSession::IsBot()               WorldSession::IsHeadless()
 *   IWorld::AddQueryHolderCallback      moved to WorldSession
 *   MotionMaster::MoveKnockbackFrom     MoveKnockbackFrom(..., bool
 *     (4 args; skips client-controlled    allowClientControlled = false);
 *     players, MoveKnockbackFromFor-      see DcCoreCompat::MoveKnockbackFrom
 *     Player does not)
 *
 * Everything that has a spelling valid on both cores uses it and is not
 * mentioned here. What remains is detected by mod-dungeon-clear.cmake, which
 * greps the exact token out of the core header (an enum value and a virtual
 * member cannot be tested by the preprocessor):
 *
 *   DC_CORE_HAS_ON_PACKET_SENT       ServerScript.h declares SERVERHOOK_ON_PACKET_SENT
 *   DC_CORE_IWORLD_HAS_QUERYHOLDER   IWorld.h still declares AddQueryHolderCallback
 *
 * Every #if on these is deletable, along with this header, once no supported
 * core lacks the upstream hooks.
 */

#ifndef _DUNGEONCLEAR_DCCORECOMPAT_H
#define _DUNGEONCLEAR_DCCORECOMPAT_H

#include "MotionMaster.h"
#include "Unit.h"
#include "WorldSession.h"

namespace DcCoreCompat
{
    namespace Detail
    {
        template <typename S>
        bool IsClientless(S const* session)
        {
            if constexpr (requires { session->IsHeadless(); })
                return session->IsHeadless();
            else
                return session->IsBot();
        }

        // Templated on both types so a test can drive either core's shape, and
        // so the branch for the other core is never instantiated.
        template <typename M, typename U>
        void MoveKnockbackFrom(M& motion, U* owner, float srcX, float srcY, float speedXY, float speedZ)
        {
            if constexpr (requires { motion.MoveKnockbackFrom(srcX, srcY, speedXY, speedZ, true); })
                motion.MoveKnockbackFrom(srcX, srcY, speedXY, speedZ, true);
            else if (owner->IsPlayer() && owner->IsClientControlled())
                motion.MoveKnockbackFromForPlayer(srcX, srcY, speedXY, speedZ);
            else
                motion.MoveKnockbackFrom(srcX, srcY, speedXY, speedZ);
        }
    }

    // A session with no game client behind it: a playerbot. Headless on the
    // new core; the fork core spells it IsBot(). Where both exist, IsBot() is
    // the one to avoid: #2792 stops passing is_bot, so it reads false for bots.
    inline bool IsClientless(WorldSession const* session)
    {
        return session && Detail::IsClientless(session);
    }

    // Knock a unit back from (srcX, srcY), client-controlled players included.
    // The new core spells that allowClientControlled = true. The fork core's
    // MoveKnockbackFrom has no such argument and returns early for a
    // client-controlled player, so that case goes to MoveKnockbackFromForPlayer,
    // the fork's own unguarded copy.
    inline void MoveKnockbackFrom(Unit* unit, float srcX, float srcY, float speedXY, float speedZ)
    {
        if (unit)
            Detail::MoveKnockbackFrom(*unit->GetMotionMaster(), unit, srcX, srcY, speedXY, speedZ);
    }
}

#endif
