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

#include "ScriptMgr.h"
#include "CharacterCache.h"
#include "ChatCommandTags.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Language.h"
#include "LFG.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

class group_commandscript : public CommandScript
{
public:
    group_commandscript() : CommandScript("group_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> groupSetCommandTable =
        {
            { "leader",     rbac::RBAC_PERM_COMMAND_GROUP_LEADER,     false, &HandleGroupLeaderCommand,     "" },
            { "assistant",  rbac::RBAC_PERM_COMMAND_GROUP_ASSISTANT,  false, &HandleGroupAssistantCommand,  "" },
            { "maintank",   rbac::RBAC_PERM_COMMAND_GROUP_MAINTANK,   false, &HandleGroupMainTankCommand,   "" },
            { "mainassist", rbac::RBAC_PERM_COMMAND_GROUP_MAINASSIST, false, &HandleGroupMainAssistCommand, "" }
        };

        static std::vector<ChatCommand> groupCommandTable =
        {
            { "set",     rbac::RBAC_PERM_COMMAND_GROUP_SET,       false, nullptr,                    "", groupSetCommandTable },
            { "leader",  rbac::RBAC_PERM_COMMAND_GROUP_LEADER,    false, &HandleGroupLeaderCommand,  "" },
            { "disband", rbac::RBAC_PERM_COMMAND_GROUP_DISBAND,   false, &HandleGroupDisbandCommand, "" },
            { "remove",  rbac::RBAC_PERM_COMMAND_GROUP_REMOVE,    false, &HandleGroupRemoveCommand,  "" },
            { "join",    rbac::RBAC_PERM_COMMAND_GROUP_JOIN,      false, &HandleGroupJoinCommand,    "" },
            { "list",    rbac::RBAC_PERM_COMMAND_GROUP_LIST,      false, &HandleGroupListCommand,    "" },
            { "summon",  rbac::RBAC_PERM_COMMAND_GROUP_SUMMON,    false, &HandleGroupSummonCommand,  "" },
            { "revive",  rbac::RBAC_PERM_COMMAND_REVIVE,          true,  &HandleGroupReviveCommand,  "" },
            { "repair",  rbac::RBAC_PERM_COMMAND_REPAIRITEMS,     true,  &HandleGroupRepairCommand,  "" },
            { "level",   rbac::RBAC_PERM_COMMAND_CHARACTER_LEVEL, true,  &HandleGroupLevelCommand,   "" }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "group", rbac::RBAC_PERM_COMMAND_GROUP, false, nullptr, "", groupCommandTable },
        };
        return commandTable;
    }

    static bool HandleGroupLevelCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, int16 level)
    {
        if (level < 1)
            return false;
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        Player* target = player->GetConnectedPlayer();
        if (!target)
            return false;

        Group* groupTarget = target->GetGroup();
        if (!groupTarget)
            return false;

        for (GroupReference const& it : groupTarget->GetMembers())
        {
            target = it.GetSource();
            uint8 oldlevel = static_cast<uint8>(target->GetLevel());

            if (level != oldlevel)
            {
                target->SetLevel(static_cast<uint8>(level));
                target->InitTalentForLevel();
                target->SetXP(0);
            }

            if (handler->needReportToTarget(target))
            {
                if (oldlevel < static_cast<uint8>(level))
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_UP, handler->GetNameLink().c_str(), level);
                else                                                // if (oldlevel > newlevel)
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_DOWN, handler->GetNameLink().c_str(), level);
            }
        }
        return true;
    }

    static bool HandleGroupReviveCommand(ChatHandler* handler, char const* args)
    {
        Player* playerTarget;
        if (!handler->extractPlayerTarget((char*)args, &playerTarget))
            return false;

        Group* groupTarget = playerTarget->GetGroup();
        if (!groupTarget)
            return false;

        for (GroupReference const& it : groupTarget->GetMembers())
        {
            Player* target = it.GetSource();
            target->ResurrectPlayer(target->GetSession()->HasPermission(rbac::RBAC_PERM_RESURRECT_WITH_FULL_HPS) ? 1.0f : 0.5f);
            target->SpawnCorpseBones();
            target->SaveToDB();
        }

        return true;
    }

    // Repair group of players
    static bool HandleGroupRepairCommand(ChatHandler* handler, char const* args)
    {
        Player* playerTarget;
        if (!handler->extractPlayerTarget((char*)args, &playerTarget))
            return false;

        Group* groupTarget = playerTarget->GetGroup();
        if (!groupTarget)
            return false;

        for (GroupReference const& it : groupTarget->GetMembers())
            it.GetSource()->DurabilityRepairAll(false, 0, false);

        return true;
    }

    // Summon group of player
    static bool HandleGroupSummonCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        if (!handler->extractPlayerTarget((char*)args, &target))
            return false;

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        Group* group = target->GetGroup();

        std::string nameLink = handler->GetNameLink(target);

        if (!group)
        {
            handler->PSendSysMessage(LANG_NOT_IN_GROUP, nameLink.c_str());
            return false;
        }

        Player* gmPlayer = handler->GetSession()->GetPlayer();
        Map* gmMap = gmPlayer->GetMap();
        bool toInstance = gmMap->Instanceable();
        bool onlyLocalSummon = false;

        // make sure people end up on our instance of the map, disallow far summon if intended destination is different from actual destination
        // note: we could probably relax this further by checking permanent saves and the like, but eh
        // :close enough:
        if (toInstance)
        {
            Player* groupLeader = ObjectAccessor::GetPlayer(gmMap, group->GetLeaderGUID());
            if (!groupLeader || (groupLeader->GetMapId() != gmMap->GetId()) || (groupLeader->GetInstanceId() != gmMap->GetInstanceId()))
            {
                handler->SendSysMessage(LANG_PARTIAL_GROUP_SUMMON);
                onlyLocalSummon = true;
            }
        }

        for (GroupReference const& itr : group->GetMembers())
        {
            Player* player = itr.GetSource();

            if (player == gmPlayer)
                continue;

            // check online security
            if (handler->HasLowerSecurity(player, ObjectGuid::Empty))
                continue;

            std::string plNameLink = handler->GetNameLink(player);

            if (player->IsBeingTeleported())
            {
                handler->PSendSysMessage(LANG_IS_TELEPORTED, plNameLink.c_str());
                continue;
            }

            if (toInstance)
            {
                Map* playerMap = player->GetMap();

                if (
                    (onlyLocalSummon || (playerMap->Instanceable() && playerMap->GetId() == gmMap->GetId())) && // either no far summon allowed or we're in the same map as player (no map switch)
                    ((playerMap->GetId() != gmMap->GetId()) || (playerMap->GetInstanceId() != gmMap->GetInstanceId())) // so we need to be in the same map and instance of the map, otherwise skip
                    )
                {
                    // cannot summon from instance to instance
                    handler->PSendSysMessage(LANG_CANNOT_SUMMON_INST_INST, plNameLink.c_str());
                    continue;
                }
            }

            handler->PSendSysMessage(LANG_SUMMONING, plNameLink.c_str(), "");
            if (handler->needReportToTarget(player))
                ChatHandler(player->GetSession()).PSendSysMessage(LANG_SUMMONED_BY, handler->GetNameLink().c_str());

            // stop flight if need
            if (player->IsInFlight())
                player->FinishTaxiFlight();
            else
                player->SaveRecallPosition(); // save only in non-flight case

            // before GM
            float x, y, z;
            gmPlayer->GetClosePoint(x, y, z, player->GetCombatReach());
            player->TeleportTo(gmPlayer->GetMapId(), x, y, z, player->GetOrientation(), TELE_TO_NONE, gmPlayer->GetInstanceId());
        }

        return true;
    }

    static bool HandleGroupLeaderCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (group->GetLeaderGUID() != guid)
        {
            group->ChangeLeader(guid);
            group->SendUpdate();
        }

        return true;
    }

    static bool GroupFlagCommand(ChatHandler* handler, char const* args, GroupMemberFlags flag, char const* what)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!group->isRaidGroup())
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_RAID_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (flag == MEMBER_FLAG_ASSISTANT && group->IsLeader(guid))
        {
            handler->PSendSysMessage(LANG_LEADER_CANNOT_BE_ASSISTANT, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (group->GetMemberFlags(guid) & flag)
        {
            group->SetGroupMemberFlag(guid, false, flag);
            handler->PSendSysMessage(LANG_GROUP_ROLE_CHANGED, player->GetName().c_str(), "no longer", what);
        }
        else
        {
            group->SetGroupMemberFlag(guid, true, flag);
            handler->PSendSysMessage(LANG_GROUP_ROLE_CHANGED, player->GetName().c_str(), "now", what);
        }
        return true;
    }

    static bool HandleGroupAssistantCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_ASSISTANT, "Assistant");
    }

    static bool HandleGroupMainTankCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_MAINTANK, "Main Tank");
    }

    static bool HandleGroupMainAssistCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_MAINASSIST, "Main Assist");
    }

    static bool HandleGroupDisbandCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        group->Disband();
        return true;
    }

    static bool HandleGroupRemoveCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        group->RemoveMember(guid);
        return true;
    }

    static bool HandleGroupJoinCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* playerSource = nullptr;
        Player* playerTarget = nullptr;
        Group* groupSource = nullptr;
        Group* groupTarget = nullptr;
        ObjectGuid guidSource;
        ObjectGuid guidTarget;
        char* nameplgrStr = strtok((char*)args, " ");
        char* nameplStr = strtok(nullptr, " ");

        if (!handler->GetPlayerGroupAndGUIDByName(nameplgrStr, playerSource, groupSource, guidSource, true))
            return false;

        if (!groupSource)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, playerSource->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!handler->GetPlayerGroupAndGUIDByName(nameplStr, playerTarget, groupTarget, guidTarget, true))
            return false;

        if (groupTarget || playerTarget->GetGroup() == groupSource)
        {
            handler->PSendSysMessage(LANG_GROUP_ALREADY_IN_GROUP, playerTarget->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (groupSource->IsFull())
        {
            handler->PSendSysMessage(LANG_GROUP_FULL);
            handler->SetSentErrorMessage(true);
            return false;
        }

        groupSource->AddMember(playerTarget);
        groupSource->BroadcastGroupUpdate();
        handler->PSendSysMessage(LANG_GROUP_PLAYER_JOINED, playerTarget->GetName().c_str(), playerSource->GetName().c_str());
        return true;
    }

    static bool HandleGroupListCommand(ChatHandler* handler, PlayerIdentifier const& target)
    {
        char const* zoneName = "<ERROR>";
        char const* onlineState = "Offline";

        // Next, we need a group. So we define a group variable.
        Group* groupTarget = nullptr;

        // We try to extract a group from an online player.
        if (target.IsConnected())
            groupTarget = target.GetConnectedPlayer()->GetGroup();
        else
        {
            // If not, we extract it from the SQL.
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GROUP_MEMBER);
            stmt->setUInt64(0, target.GetGUID().GetCounter());
            PreparedQueryResult resultGroup = CharacterDatabase.Query(stmt);
            if (resultGroup)
                groupTarget = sGroupMgr->GetGroupByDbStoreId((*resultGroup)[0].GetUInt32());
        }

        // If both fails, players simply has no party. Return false.
        if (!groupTarget)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, target.GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // We get the group members after successfully detecting a group.
        Group::MemberSlotList const& members = groupTarget->GetMemberSlots();

        // To avoid a cluster fuck, namely trying multiple queries to simply get a group member count...
        handler->PSendSysMessage(LANG_GROUP_TYPE, (groupTarget->isRaidGroup() ? "raid" : "party"), std::to_string(members.size()).c_str());
        // ... we simply move the group type and member count print after retrieving the slots and simply output it's size.

        // While rather dirty codestyle-wise, it saves space (if only a little). For each member, we look several informations up.
        for (Group::MemberSlotList::const_iterator itr = members.begin(); itr != members.end(); ++itr)
        {
            // Define temporary variable slot to iterator.
            Group::MemberSlot const& slot = *itr;

            // Check for given flag and assign it to that iterator
            std::string flags;
            if (slot.flags & MEMBER_FLAG_ASSISTANT)
                flags = "Assistant";

            if (slot.flags & MEMBER_FLAG_MAINTANK)
            {
                if (!flags.empty())
                    flags.append(", ");
                flags.append("MainTank");
            }

            if (slot.flags & MEMBER_FLAG_MAINASSIST)
            {
                if (!flags.empty())
                    flags.append(", ");
                flags.append("MainAssist");
            }

            if (flags.empty())
                flags = "None";

            // Check if iterator is online. If is...
            Player* p = ObjectAccessor::FindPlayer((*itr).guid);
            std::string phases;
            if (p)
            {
                // ... than, it prints information like "is online", where he is, etc...
                onlineState = "online";
                LocaleConstant locale = handler->GetSessionDbcLocale();
                phases = PhasingHandler::FormatPhases(p->GetPhaseShift());

                AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId());
                if (area && area->GetFlags().HasFlag(AreaFlags::IsSubzone))
                {
                    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(area->ParentAreaID);
                    if (zone)
                        zoneName = zone->AreaName[locale];
                }
            }

            // Now we can print those informations for every single member of each group!
            handler->PSendSysMessage(LANG_GROUP_PLAYER_NAME_GUID, slot.name.c_str(), onlineState,
                zoneName, phases.c_str(), slot.guid.ToString().c_str(), flags.c_str(),
                lfg::GetRolesString(slot.roles).c_str());
        }

        // And finish after every iterator is done.
        return true;
    }
};

void AddSC_group_commandscript()
{
    new group_commandscript();
}
