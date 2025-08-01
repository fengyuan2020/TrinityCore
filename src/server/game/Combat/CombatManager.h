/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITY_COMBATMANAGER_H
#define TRINITY_COMBATMANAGER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <unordered_map>

class Unit;

/********************************************************************************************************************************************************\
 *                                                           DEV DOCUMENTATION: COMBAT SYSTEM                                                           *
 *                                            (future devs: please keep this up-to-date if you change the system)                                       *
 * CombatManager maintains a list of dynamically allocated CombatReference entries. Each entry represents a combat state between two distinct units.    *
 * A unit is "in combat" iff it has one or more non-suppressed CombatReference entries in its CombatManager. No exceptions.                             *
 *                                                                                                                                                      *
 * A CombatReference object carries the following implicit guarantees by existing:                                                                      *
 *  - Both CombatReference.first and CombatReference.second are valid Units, distinct, not nullptr and currently in the world.                          *
 *  - If the CombatReference was retrieved from the CombatManager of Unit* A, then exactly one of .first and .second is equal to A.                     *
 *    - Note: Use CombatReference::GetOther to quickly get the other unit for a given reference.                                                        *
 *  - Both .first and .second are currently in combat (IsInCombat will always be true) if either of the following hold:                                 *
 *    - IsSuppressedFor returns false for the respective unit                                                                                           *
 *                                                                                                                                                      *
 * To end combat between two units, find their CombatReference and call EndCombat.                                                                      *
 *  - Keep in mind that this modifies the CombatRefs maps on both ends, which may cause iterators to be invalidated.                                    *
 *                                                                                                                                                      *
 * To put two units in combat with each other, call SetInCombatWith. Note that this is not guaranteed to succeed.                                       *
 *  - The return value of SetInCombatWith is the new combat state between the units (identical to calling IsInCombatWith at that time).                 *
 *                                                                                                                                                      *
 * Note that (threat => combat) is a strong guarantee provided in conjunction with ThreatManager. Thus:                                                 *
 *  - Ending combat between two units will also delete any threat references that may exist between them.                                               *
 *  - Adding threat will also create a combat reference between the units if one doesn't exist yet.                                                     *
\********************************************************************************************************************************************************/

// Please check Game/Combat/CombatManager.h for documentation on how this class works!
struct TC_GAME_API CombatReference
{
    Unit* const first;
    Unit* const second;
    bool const _isPvP;
    Unit* GetOther(Unit const* me) const { return (first == me) ? second : first; }

    void EndCombat();

    // suppressed combat refs do not generate a combat state for one side of the relation
    // (used by: vanish, feign death and launched out of combat but not yet landed spell missiles)
    void SuppressFor(Unit* who);
    bool IsSuppressedFor(Unit const* who) const { return (who == first) ? _suppressFirst : _suppressSecond; }

    CombatReference(CombatReference const&) = delete;
    CombatReference& operator=(CombatReference const&) = delete;

protected:
    CombatReference(Unit* a, Unit* b, bool pvp = false) : first(a), second(b), _isPvP(pvp) { }

    void Refresh();
    void Suppress(Unit* who) { (who == first ? _suppressFirst : _suppressSecond) = true; }

    bool _suppressFirst = false;
    bool _suppressSecond = false;

    friend class CombatManager;
};

// Please check Game/Combat/CombatManager.h for documentation on how this class works!
struct TC_GAME_API PvPCombatReference : public CombatReference
{
    static const uint32 PVP_COMBAT_TIMEOUT = 5 * IN_MILLISECONDS;

private:
    PvPCombatReference(Unit* first, Unit* second) : CombatReference(first, second, true) { }

    bool Update(uint32 tdiff);
    void RefreshTimer();

    uint32 _combatTimer = PVP_COMBAT_TIMEOUT;

    friend class CombatManager;
};

// please check Game/Combat/CombatManager.h for documentation on how this class works!
class TC_GAME_API CombatManager
{
    public:
        static bool CanBeginCombat(Unit const* a, Unit const* b);

        CombatManager(Unit* owner);
        ~CombatManager();
        void Update(uint32 tdiff); // called from Unit::Update

        Unit* GetOwner() const { return _owner; }
        bool HasCombat() const { return HasPvECombat() || HasPvPCombat(); }
        bool HasPvECombat() const;
        bool HasPvECombatWithPlayers() const;
        std::unordered_map<ObjectGuid, CombatReference*> const& GetPvECombatRefs() const { return _pveRefs; }
        bool HasPvPCombat() const;
        std::unordered_map<ObjectGuid, PvPCombatReference*> const& GetPvPCombatRefs() const { return _pvpRefs; }
        // If the Unit is in combat, returns an arbitrary Unit that it's in combat with. Otherwise, returns nullptr.
        Unit* GetAnyTarget() const;

        // return value is the same as calling IsInCombatWith immediately after this returns
        bool SetInCombatWith(Unit* who, bool addSecondUnitSuppressed = false);
        bool IsInCombatWith(ObjectGuid const& who) const;
        bool IsInCombatWith(Unit const* who) const;
        void InheritCombatStatesFrom(Unit const* who);
        void EndCombatBeyondRange(float range, bool includingPvP = false);

        using UnitFilter = bool(Unit const* otherUnit);

        // flags any pvp refs for suppression on owner's side - these refs will not generate combat until refreshed
        void SuppressPvPCombat(UnitFilter* unitFilter = nullptr);
        void EndAllPvECombat(UnitFilter* unitFilter = nullptr);
        void RevalidateCombat();
        void EndAllPvPCombat(UnitFilter* unitFilter = nullptr);
        void EndAllCombat(UnitFilter* unitFilter = nullptr) { EndAllPvECombat(unitFilter); EndAllPvPCombat(unitFilter); }

        CombatManager(CombatManager const&) = delete;
        CombatManager& operator=(CombatManager const&) = delete;

    private:
        static void NotifyAICombat(Unit* me, Unit* other);
        void PutReference(ObjectGuid const& guid, CombatReference* ref);
        void PurgeReference(ObjectGuid const& guid, bool pvp);
        bool UpdateOwnerCombatState() const;
        Unit* const _owner;
        std::unordered_map<ObjectGuid, CombatReference*> _pveRefs;
        std::unordered_map<ObjectGuid, PvPCombatReference*> _pvpRefs;

    friend struct CombatReference;
    friend struct PvPCombatReference;
};

#endif
