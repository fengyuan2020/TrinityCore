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

#ifndef ScenarioMgr_h__
#define ScenarioMgr_h__

#include "Common.h"
#include "Hash.h"
#include "SharedDefines.h"
#include <map>
#include <unordered_map>
#include <vector>

class InstanceMap;
class InstanceScenario;
struct ScenarioEntry;
struct ScenarioStepEntry;

struct ScenarioData
{
    ScenarioEntry const* Entry;
    std::map<uint8, ScenarioStepEntry const*> Steps;
};

/*
    Scenario data should be loaded on demand.
    The server will get data from the database which scenario ids is linked with which map id/difficulty/player team.
    The first time a scenario is loaded, the map loads and stores the scenario data for future scenario instance launches.
*/

struct ScenarioDBData
{
    uint32 MapID;
    uint8 DifficultyID;
    uint32 Scenario_A;
    uint32 Scenario_H;
};

typedef std::unordered_map<std::pair<uint32, uint8>, ScenarioDBData> ScenarioDBDataContainer;
typedef std::map<uint32, ScenarioData> ScenarioDataContainer;

enum ScenarioType
{
    SCENARIO_TYPE_SCENARIO          = 0,
    SCENARIO_TYPE_CHALLENGE_MODE    = 1,
    SCENARIO_TYPE_SOLO              = 2,
    SCENARIO_TYPE_DUNGEON           = 10,
};

struct ScenarioPOIPoint
{
    int32 X;
    int32 Y;
    int32 Z;

    ScenarioPOIPoint() : X(0), Y(0), Z(0) { }
    ScenarioPOIPoint(int32 x, int32 y, int32 z) : X(x), Y(y), Z(z) { }
};

struct ScenarioPOI
{
    int32 BlobIndex;
    int32 MapID;
    int32 UiMapID;
    int32 Priority;
    int32 Flags;
    int32 WorldEffectID;
    int32 PlayerConditionID;
    int32 NavigationPlayerConditionID;
    std::vector<ScenarioPOIPoint> Points;

    ScenarioPOI() : BlobIndex(0), MapID(0), UiMapID(0), Priority(0), Flags(0), WorldEffectID(0), PlayerConditionID(0), NavigationPlayerConditionID(0) { }

    ScenarioPOI(int32 blobIndex, int32 mapID, int32 uiMapID, int32 priority, int32 flags, int32 worldEffectID,
        int32 playerConditionID, int32 navigationPlayerConditionID, std::vector<ScenarioPOIPoint> points) :
        BlobIndex(blobIndex), MapID(mapID), UiMapID(uiMapID), Priority(priority), Flags(flags), WorldEffectID(worldEffectID),
        PlayerConditionID(playerConditionID), NavigationPlayerConditionID(navigationPlayerConditionID), Points(std::move(points)) { }

    ScenarioPOI(ScenarioPOI&& scenarioPOI) = default;
};

typedef std::vector<ScenarioPOI> ScenarioPOIVector;
typedef std::unordered_map<uint32, ScenarioPOIVector> ScenarioPOIContainer;

class TC_GAME_API ScenarioMgr
{
private:
    ScenarioMgr();
    ~ScenarioMgr();

public:
    ScenarioMgr(ScenarioMgr const&) = delete;
    ScenarioMgr(ScenarioMgr&&) = delete;
    ScenarioMgr& operator=(ScenarioMgr const&) = delete;
    ScenarioMgr& operator=(ScenarioMgr&&) = delete;

    static ScenarioMgr* Instance();

    InstanceScenario* CreateInstanceScenarioForTeam(InstanceMap* map, TeamId team) const;
    InstanceScenario* CreateInstanceScenario(InstanceMap* map, uint32 scenarioID) const;

    void LoadDBData();
    void LoadDB2Data();
    void LoadScenarioPOI();

    ScenarioPOIVector const* GetScenarioPOIs(int32 criteriaTreeID) const;

private:
    ScenarioDataContainer _scenarioData;
    ScenarioPOIContainer _scenarioPOIStore;
    ScenarioDBDataContainer _scenarioDBData;
};

#define sScenarioMgr ScenarioMgr::Instance()

#endif // ScenarioMgr_h__
