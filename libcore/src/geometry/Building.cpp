/**
 * \file        Building.cpp
 * \date        Oct 1, 2014
 * \version     v0.7
 * \copyright   <2009-2015> Forschungszentrum Jülich GmbH. All rights reserved.
 *
 * \section License
 * This file is part of JuPedSim.
 *
 * JuPedSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * JuPedSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with JuPedSim. If not, see <http://www.gnu.org/licenses/>.
 *
 * \section Description
 *
 *
 **/
#include "Building.h"

#include "general/OpenMP.h"
#include "geometry/SubRoom.h"
#include "geometry/Wall.h"

#include <chrono>
#include <tinyxml.h>

#ifdef _SIMULATOR
#include "GeometryReader.h"
#include "IO/GeoFileParser.h"
#include "general/Filesystem.h"
#include "general/Logger.h"
#include "mpi/LCGrid.h"
#include "pedestrian/Pedestrian.h"

#include <thread>
#endif

Building::Building()
{
    _caption          = "no_caption";
    _geometryFilename = "";
    _routingEngine    = nullptr;
    _linkedCellGrid   = nullptr;
    _savePathway      = false;
}

#ifdef _SIMULATOR

Building::Building(Configuration * configuration, PedDistributor & pedDistributor) :
    _configuration(configuration),
    _routingEngine(configuration->GetRoutingEngine()),
    _caption("no_caption")
{
    _savePathway    = false;
    _linkedCellGrid = nullptr;

    {
        std::unique_ptr<GeoFileParser> parser(new GeoFileParser(_configuration));
        parser->LoadBuilding(this);
    }

    if(!InitGeometry()) {
        LOG_ERROR("Could not initialize the geometry!");
        exit(EXIT_FAILURE);
    }


    //TODO: check whether traffic info can be loaded before InitGeometry if so call it in LoadBuilding instead and make
    //TODO: LoadTrafficInfo private [gl march '16]


    if(!pedDistributor.Distribute(this)) {
        LOG_ERROR("Could not distribute the pedestrians");
        exit(EXIT_FAILURE);
    }
    InitGrid();

    if(!_routingEngine->Init(this)) {
        LOG_ERROR("Could not initialize the routers!");
        exit(EXIT_FAILURE);
    }

    if(!SanityCheck()) {
        LOG_ERROR("There are sanity errors in the geometry file");
        exit(EXIT_FAILURE);
    }
}

#endif

Building::~Building()
{
#ifdef _SIMULATOR
    for(auto & pedestrian : _allPedestrians) {
        delete pedestrian;
    }
    _allPedestrians.clear();
    delete _linkedCellGrid;
#endif

    if(_pathWayStream.is_open())
        _pathWayStream.close();

    for(std::map<int, Crossing *>::const_iterator iter = _crossings.begin();
        iter != _crossings.end();
        ++iter) {
        // Don't delete crossings from goals
        bool goalCrossing = false;
        for(auto [_, goal] : _goals) {
            if(goal->GetCentreCrossing()->GetUniqueID() == iter->second->GetUniqueID()) {
                goalCrossing = true;
            }
        }
        if(!goalCrossing) {
            delete iter->second;
        }
    }
    for(std::map<int, Transition *>::const_iterator iter = _transitions.begin();
        iter != _transitions.end();
        ++iter) {
        delete iter->second;
    }
    for(std::map<int, Hline *>::const_iterator iter = _hLines.begin(); iter != _hLines.end();
        ++iter) {
        delete iter->second;
    }
    for(std::map<int, Goal *>::const_iterator iter = _goals.begin(); iter != _goals.end(); ++iter) {
        delete iter->second;
    }
}

Configuration * Building::GetConfig() const
{
    return _configuration;
}

void Building::SetCaption(const std::string & s)
{
    _caption = s;
}

std::string Building::GetCaption() const
{
    return _caption;
}

RoutingEngine * Building::GetRoutingEngine() const
{
    return _configuration->GetRoutingEngine().get();
}

int Building::GetNumberOfRooms() const
{
    return (int) _rooms.size();
}

int Building::GetNumberOfGoals() const
{
    return (int) (_transitions.size() + _hLines.size() + _crossings.size());
}

const std::map<int, std::shared_ptr<Room>> & Building::GetAllRooms() const
{
    return _rooms;
}

Room * Building::GetRoom(int index) const
{
    //TODO: obsolete since the check is done by .at()
    if(_rooms.count(index) == 0) {
        LOG_ERROR(
            "Wrong 'index' in CBuiling::GetRoom() Room ID: {} size: {}", index, _rooms.size());
        LOG_INFO("Control your rooms ID and make sure they are in the order 0, 1, 2,.. ");
        return nullptr;
    }
    return _rooms.at(index).get();
}

LCGrid * Building::GetGrid() const
{
    return _linkedCellGrid;
}

void Building::AddRoom(Room * room)
{
    _rooms[room->GetID()] = std::shared_ptr<Room>(room);
}

void Building::AddSurroundingRoom()
{
    LOG_INFO("Adding the room 'outside' ");
    // first look for the geometry boundaries
    double x_min = FLT_MAX;
    double x_max = -FLT_MAX;
    double y_min = FLT_MAX;
    double y_max = -FLT_MAX;
    //finding the bounding of the grid
    // and collect the pedestrians

    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            for(auto && wall : itr_subroom.second->GetAllWalls()) {
                double x1 = wall.GetPoint1()._x;
                double y1 = wall.GetPoint1()._y;
                double x2 = wall.GetPoint2()._x;
                double y2 = wall.GetPoint2()._y;

                double xmax = (x1 > x2) ? x1 : x2;
                double xmin = (x1 > x2) ? x2 : x1;
                double ymax = (y1 > y2) ? y1 : y2;
                double ymin = (y1 > y2) ? y2 : y1;

                x_min = (xmin <= x_min) ? xmin : x_min;
                x_max = (xmax >= x_max) ? xmax : x_max;
                y_max = (ymax >= y_max) ? ymax : y_max;
                y_min = (ymin <= y_min) ? ymin : y_min;
            }
        }
    }

    for(auto && itr_goal : _goals) {
        for(auto && wall : itr_goal.second->GetAllWalls()) {
            double x1 = wall.GetPoint1()._x;
            double y1 = wall.GetPoint1()._y;
            double x2 = wall.GetPoint2()._x;
            double y2 = wall.GetPoint2()._y;

            double xmax = (x1 > x2) ? x1 : x2;
            double xmin = (x1 > x2) ? x2 : x1;
            double ymax = (y1 > y2) ? y1 : y2;
            double ymin = (y1 > y2) ? y2 : y1;

            x_min = (xmin <= x_min) ? xmin : x_min;
            x_max = (xmax >= x_max) ? xmax : x_max;
            y_max = (ymax >= y_max) ? ymax : y_max;
            y_min = (ymin <= y_min) ? ymin : y_min;
        }
    }
    //make the grid slightly larger.
    x_min = x_min - 10.0;
    x_max = x_max + 10.0;
    y_min = y_min - 10.0;
    y_max = y_max + 10.0;

    SubRoom * bigSubroom = new NormalSubRoom();
    bigSubroom->SetRoomID((int) _rooms.size());
    bigSubroom->SetSubRoomID(0); // should be the single subroom
    bigSubroom->AddWall(Wall(Point(x_min, y_min), Point(x_min, y_max)));
    bigSubroom->AddWall(Wall(Point(x_min, y_max), Point(x_max, y_max)));
    bigSubroom->AddWall(Wall(Point(x_max, y_max), Point(x_max, y_min)));
    bigSubroom->AddWall(Wall(Point(x_max, y_min), Point(x_min, y_min)));

    Room * bigRoom = new Room();
    bigRoom->AddSubRoom(bigSubroom);
    bigRoom->SetCaption("outside");
    bigRoom->SetID((int) _rooms.size());
    AddRoom(bigRoom);
}

bool Building::InitGeometry()
{
    LOG_INFO("Init Geometry");
    correct();
    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            //create a close polyline out of everything
            std::vector<Line *> goals = std::vector<Line *>();

            //  collect all crossings
            for(auto && cros : itr_subroom.second->GetAllCrossings()) {
                goals.push_back(cros);
            }
            //collect all transitions
            for(auto && trans : itr_subroom.second->GetAllTransitions()) {
                goals.push_back(trans);
            }
            // initialize the poly
            if(!itr_subroom.second->ConvertLineToPoly(goals))
                return false;
            itr_subroom.second->CalculateArea();

            //do the same for the obstacles that are closed
            for(auto && obst : itr_subroom.second->GetAllObstacles()) {
                if(!obst->ConvertLineToPoly())
                    return false;
            }

            //here we can create a boost::geometry::model::polygon out of the vector<Point> objects created above
            itr_subroom.second->CreateBoostPoly();

            double minElevation = FLT_MAX;
            double maxElevation = -FLT_MAX;
            for(auto && wall : itr_subroom.second->GetAllWalls()) {
                const Point & P1 = wall.GetPoint1();
                const Point & P2 = wall.GetPoint2();
                if(minElevation > itr_subroom.second->GetElevation(P1)) {
                    minElevation = itr_subroom.second->GetElevation(P1);
                }

                if(maxElevation < itr_subroom.second->GetElevation(P1)) {
                    maxElevation = itr_subroom.second->GetElevation(P1);
                }

                if(minElevation > itr_subroom.second->GetElevation(P2)) {
                    minElevation = itr_subroom.second->GetElevation(P2);
                }

                if(maxElevation < itr_subroom.second->GetElevation(P2)) {
                    maxElevation = itr_subroom.second->GetElevation(P2);
                }
            }
            itr_subroom.second->SetMaxElevation(maxElevation);
            itr_subroom.second->SetMinElevation(minElevation);
        }
    }


    // look and save the neighbor subroom for improving the runtime
    // that information is already present in the crossing/transitions

    for(const auto & cross : _crossings) {
        SubRoom * s1 = cross.second->GetSubRoom1();
        SubRoom * s2 = cross.second->GetSubRoom2();
        if(s1)
            s1->AddNeighbor(s2);
        if(s2)
            s2->AddNeighbor(s1);
    }

    for(const auto & trans : _transitions) {
        SubRoom * s1 = trans.second->GetSubRoom1();
        SubRoom * s2 = trans.second->GetSubRoom2();
        if(s1)
            s1->AddNeighbor(s2);
        if(s2)
            s2->AddNeighbor(s1);
    }

    InitInsideGoals();
    InitPlatforms();
    //---
    for(auto platform : _platforms) {
        std::cout << "\n platform " << platform.first << ", " << platform.second->id << "\n";
        std::cout << "\t rid " << platform.second->rid << "\n";
        auto tracks = platform.second->tracks;
        for(auto track : tracks) {
            std::cout << "\t track " << track.first << "\n";
            auto walls = track.second;
            for(auto wall : walls) {
                std::cout << "\t\t wall: " << wall.GetType() << ". " << wall.GetPoint1().toString()
                          << " | " << wall.GetPoint2().toString() << "\n";
            }
        }
    }
    LOG_INFO("Init Geometry successful!!!");

    return true;
}

const std::vector<Wall>
Building::GetTrackWalls(Point TrackStart, Point TrackEnd, int & room_id, int & subroom_id) const
{
    bool trackFound = false;
    int track_id    = -1;
    int platform_id = -1;
    std::vector<Wall> mytrack;
    for(auto platform : _platforms) {
        platform_id = platform.second->id;
        auto tracks = platform.second->tracks;
        for(auto track : tracks) {
            track_id         = track.first;
            int commonPoints = 0;
            auto walls       = track.second;
            for(auto wall : walls) {
                Point P1 = wall.GetPoint1();
                Point P2 = wall.GetPoint2();
                if(P1 == TrackStart)
                    commonPoints++;
                if(P1 == TrackEnd)
                    commonPoints++;
                if(P2 == TrackStart)
                    commonPoints++;
                if(P2 == TrackEnd)
                    commonPoints++;
            }
            if(commonPoints == 2) {
                trackFound = true;
                break;
            }
        } // tracks
        if(trackFound)
            break;
    } // plattforms
    if(trackFound) {
        room_id    = _platforms.at(platform_id)->rid;
        subroom_id = _platforms.at(platform_id)->sid;
        mytrack    = _platforms.at(platform_id)->tracks[track_id];
    } else {
        std::cout << "could not find any track! Exit.\n";
        exit(-1);
    }
    return mytrack;
}

const std::vector<std::pair<PointWall, PointWall>> Building::GetIntersectionPoints(
    const std::vector<Transition> doors,
    const std::vector<Wall> mytrack) const
{
    const int scaleFactor = 1000; // very long orthogonal walls to train's doors
    std::vector<std::pair<PointWall, PointWall>> pws;
    // every door has two points.
    // for every point -> get pair<P, W>
    // collect pairs of pairs
    for(auto door : doors) {
        PointWall pw1, pw2;
        int nintersections = 0;
        auto n             = door.NormalVec();
        auto p11           = door.GetPoint1() + n * scaleFactor;
        auto p12           = door.GetPoint1() - n * scaleFactor;
        auto p21           = door.GetPoint2() + n * scaleFactor;
        auto p22           = door.GetPoint2() - n * scaleFactor;
        auto normalWall1   = Wall(p11, p12);
        auto normalWall2   = Wall(p21, p22);
        for(auto twall : mytrack) {
            Point interPoint1, interPoint2;
            auto res  = normalWall1.IntersectionWith(twall, interPoint1);
            auto res2 = normalWall2.IntersectionWith(twall, interPoint2);
            if(res == 1) {
                if(!twall.NearlyHasEndPoint(interPoint1)) {
                    pw1 = std::make_pair(interPoint1, twall);
                    nintersections++;
                } else // end point
                {
                    if(res2 == 0) {
                    } else {
                        pw1 = std::make_pair(interPoint1, twall);
                        nintersections++;
                    }
                }
            }
            if(res2 == 1) {
                if(!twall.NearlyHasEndPoint(interPoint2)) {
                    pw2 = std::make_pair(interPoint2, twall);
                    nintersections++;
                } else {
                    if(res == 0) {
                    } else {
                        pw2 = std::make_pair(interPoint2, twall);
                        nintersections++;
                    }
                }
            }


        } // tracks

        if(nintersections == 2)
            pws.push_back(std::make_pair(pw1, pw2));

        else {
            std::cout << "Error in GetIntersection. Should be 2 but got  " << nintersections
                      << "\n";
            exit(-1);
        }
    } // doors

    return pws;
}

// reset changes made by trainTimeTable[id]
bool Building::resetGeometry(std::shared_ptr<TrainTimeTable> tab)
{
    // this function is composed of three copy/pasted blocks.
    int room_id, subroom_id;

    // remove temp added walls
    auto tempWalls = TempAddedWalls[tab->id];
    for(auto it = tempWalls.begin(); it != tempWalls.end();) {
        auto wall = *it;
        if(it != tempWalls.end()) {
            tempWalls.erase(it);
        }
        for(auto platform : _platforms) {
            room_id    = platform.second->rid;
            subroom_id = platform.second->sid;
            SubRoom * subroom =
                this->GetAllRooms().at(room_id)->GetAllSubRooms().at(subroom_id).get();
            for(auto subWall : subroom->GetAllWalls()) {
                if(subWall == wall) {
                    // if everything goes right, then we should enter this
                    // if. We already erased from tempWalls!
                    subroom->RemoveWall(wall);
                } //if
            }     //subroom
        }         //platforms
    }
    TempAddedWalls[tab->id] = tempWalls;

    /*       // add remove walls */
    auto tempRemovedWalls = TempRemovedWalls[tab->id];
    for(auto it = tempRemovedWalls.begin(); it != tempRemovedWalls.end();) {
        auto wall = *it;
        if(it != tempRemovedWalls.end()) {
            tempRemovedWalls.erase(it);
        }
        for(auto platform : _platforms) {
            auto tracks = platform.second->tracks;
            room_id     = platform.second->rid;
            subroom_id  = platform.second->sid;
            SubRoom * subroom =
                this->GetAllRooms().at(room_id)->GetAllSubRooms().at(subroom_id).get();
            for(auto track : tracks) {
                auto walls = track.second;
                for(auto trackWall : walls) {
                    if(trackWall == wall) {
                        subroom->AddWall(wall);
                    }
                }
            }
        }
    }
    TempRemovedWalls[tab->id] = tempRemovedWalls;

    /*       // remove added doors */
    auto tempDoors = TempAddedDoors[tab->id];
    for(auto it = tempDoors.begin(); it != tempDoors.end();) {
        auto door = *it;
        if(it != tempDoors.end()) {
            tempDoors.erase(it);
        }
        for(auto platform : _platforms) {
            auto tracks = platform.second->tracks;
            room_id     = platform.second->rid;
            subroom_id  = platform.second->sid;
            SubRoom * subroom =
                this->GetAllRooms().at(room_id)->GetAllSubRooms().at(subroom_id).get();
            for(auto subTrans : subroom->GetAllTransitions()) {
                if(*subTrans == door) {
                    // Trnasitions are added to subrooms and building!!
                    subroom->RemoveTransition(subTrans);
                    this->RemoveTransition(subTrans);
                }
            }
        }
    }
    TempAddedDoors[tab->id] = tempDoors;
    return true;
}
void Building::InitPlatforms()
{
    int num_platform = -1;
    for(auto & roomItr : _rooms) {
        Room * room = roomItr.second.get();
        for(auto & subRoomItr : room->GetAllSubRooms()) {
            auto subRoom   = subRoomItr.second.get();
            int subroom_id = subRoom->GetSubRoomID();
            if(subRoom->GetType() != "Platform")
                continue;
            std::map<int, std::vector<Wall>> tracks;
            num_platform++;
            for(auto && wall : subRoom->GetAllWalls()) {
                if(wall.GetType().find("track") == std::string::npos)
                    continue;
                // add wall to track
                std::vector<std::string> strs;
                boost::split(strs, wall.GetType(), boost::is_any_of("-"), boost::token_compress_on);
                if(strs.size() <= 1)
                    continue;
                int n = atoi(strs[1].c_str());
                tracks[n].push_back(wall);
            } //walls
            std::shared_ptr<Platform> p = std::make_shared<Platform>(Platform{
                num_platform,
                room->GetID(),
                subroom_id,
                tracks,
            });
            AddPlatform(p);
        }
    }
}

bool Building::InitInsideGoals()
{
    bool found = false;
    for(auto & goalItr : _goals) {
        Goal * goal = goalItr.second;
        if(goal->GetRoomID() == -1) {
            found = true;
            continue;
        }

        for(auto & roomItr : _rooms) {
            Room * room = roomItr.second.get();

            if(goal->GetRoomID() != room->GetID()) {
                continue;
            }

            for(auto & subRoomItr : room->GetAllSubRooms()) {
                SubRoom * subRoom = subRoomItr.second.get();

                if((goal->GetSubRoomID() == subRoom->GetSubRoomID()) &&
                   (subRoom->IsInSubRoom(goal->GetCentroid()))) {
                    Crossing * crossing = goal->GetCentreCrossing();
                    subRoom->AddCrossing(crossing);
                    crossing->SetRoom1(room);
                    crossing->SetSubRoom1(subRoom);
                    crossing->SetSubRoom2(subRoom);
                    AddCrossing(crossing);
                    found = true;
                    break;
                }
            }
        }

        if(!found) {
            LOG_WARNING(
                "Goal {} seems to have no subroom and is not outside, please check your input",
                goal->GetId());
        }
        found = false;
    }

    LOG_INFO("InitInsideGoals successful!!!");

    return true;
}

bool Building::correct() const
{
    auto t_start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Enter correct ...");
    bool removed      = false;
    bool removed_room = false;
    for(auto && room : this->GetAllRooms()) {
        for(auto && subroom : room.second->GetAllSubRooms()) {
            // -- remove exits *on* walls
            removed_room = RemoveOverlappingDoors(subroom.second);
            removed      = removed || removed_room;

            // --------------------------
            // -- remove overlapping walls
            auto walls = subroom.second->GetAllWalls(); // this call
                                                        // should be
                                                        // after
                                                        // eliminating
                                                        // nasty exits
            LOG_DEBUG("Correct Room {} SubRoom {}", room.first, subroom.first);

            for(auto const & bigWall : walls) //self checking
            {
                std::vector<Wall> WallPieces;
                WallPieces = SplitWall(subroom.second, bigWall);
                if(!WallPieces.empty())
                    removed = true;

                LOG_DEBUG("Wall pieces size : {}", WallPieces.size());

                for(auto w : WallPieces) {
                    LOG_DEBUG("{}", w.toString());
                }

                int ok = 0;
                while(!ok) {
                    ok = 1; // ok ==1 means no new pieces are found
                    for(auto wallPiece : WallPieces) {
                        std::vector<Wall> tmpWallPieces;
                        tmpWallPieces = SplitWall(subroom.second, wallPiece);
                        if(!tmpWallPieces.empty()) {
                            ok = 0; /// stay in the loop
                            // append tmpWallPieces to WallPiece
                            WallPieces.insert(
                                std::end(WallPieces),
                                std::begin(tmpWallPieces),
                                std::end(tmpWallPieces));
                            // remove the line since it was split already
                            auto it = std::find(WallPieces.begin(), WallPieces.end(), wallPiece);
                            if(it != WallPieces.end()) {
                                WallPieces.erase(it);
                            }
                        }
                    }

                    LOG_DEBUG("Ok: {}", ok);
                    LOG_DEBUG("New while  Wall pieces size: {}", WallPieces.size());

                    for(const auto & t : WallPieces) {
                        LOG_DEBUG("Piece: {}", t.toString());
                    }

                } // while
                // remove
                // duplicates fromWllPiecs
                if(!WallPieces.empty()) {
                    //remove  duplicaes from wallPiecess

                    auto end = WallPieces.end();
                    for(auto it = WallPieces.begin(); it != end; ++it) {
                        end = std::remove(it + 1, end, *it);
                    }
                    WallPieces.erase(end, WallPieces.end());

                    LOG_DEBUG("Removing duplicates pieces.");
                    for(auto t : WallPieces) {
                        LOG_DEBUG("Piece: {}", t.toString());
                    }

                    // remove big wall and add one wallpiece to walls
                    ReplaceBigWall(subroom.second, bigWall, WallPieces);
                }
            } // bigLine
        }     //s
    }         //r

    if(removed) {
        auto geometryFile = add_prefix_to_filename("correct_", GetGeometryFilename());

        if(SaveGeometry(geometryFile)) {
            GetConfig()->SetGeometryFile(geometryFile);
        }
    }

    auto t_end           = std::chrono::high_resolution_clock::now();
    double elapsedTimeMs = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    LOG_INFO("Leave geometry correct with success ({:.3f}s)", elapsedTimeMs);
    return true;
}
bool Building::RemoveOverlappingDoors(const std::shared_ptr<SubRoom> & subroom) const
{
    LOG_DEBUG(
        "Enter RemoveOverlappingDoors with SubRoom {}, {}",
        subroom->GetRoomID(),
        subroom->GetSubRoomID());

    bool removed               = false;               // did we remove anything?
    std::vector<Line> exits    = std::vector<Line>(); // transitions+crossings
    auto walls                 = subroom->GetAllWalls();
    std::vector<Wall> tmpWalls = std::vector<Wall>(); //splited big walls are stored here
    bool isBigWall             = false; // mark walls as big. If not big, add them to tmpWalls
    //  collect all crossings
    for(auto && cros : subroom->GetAllCrossings())
        exits.push_back(*cros);
    //collect all transitions
    for(auto && trans : subroom->GetAllTransitions())
        exits.push_back(*trans);

    LOG_DEBUG("Subroom walls: ");
    for(const auto & w : subroom->GetAllWalls()) {
        LOG_DEBUG("Wall: {}", w.toString());
    }


    // removing doors on walls
    while(!walls.empty()) {
        auto wall = walls.back();
        walls.pop_back();
        isBigWall = false;
        for(auto e = exits.begin(); e != exits.end();) {
            if(wall.NearlyInLineSegment(e->GetPoint1()) &&
               wall.NearlyInLineSegment(e->GetPoint2())) {
                isBigWall       = true; // mark walls as big
                double dist_pt1 = (wall.GetPoint1() - e->GetPoint1()).NormSquare();
                double dist_pt2 = (wall.GetPoint1() - e->GetPoint2()).NormSquare();
                Point A, B;

                if(dist_pt1 < dist_pt2) {
                    A = e->GetPoint1();
                    B = e->GetPoint2();
                } else {
                    A = e->GetPoint2();
                    B = e->GetPoint1();
                }

                Wall NewWall(wall.GetPoint1(), A);
                Wall NewWall1(wall.GetPoint2(), B);
                // add new lines to be controled against overlap with exits
                walls.push_back(NewWall);
                walls.push_back(NewWall1);
                subroom->RemoveWall(wall);
                exits.erase(e); // we don't need to check this exit again
                removed = true;
                break; // we are done with this wall. get next wall.

            } // if
            else {
                e++;
            }
        } // exits
        if(!isBigWall)
            tmpWalls.push_back(wall);

    } // while

    // copy walls in subroom
    for(auto const & wall : tmpWalls) {
        subroom->AddWall(wall);
    }

    LOG_DEBUG("New Subroom:  ");
    for(const auto & w : subroom->GetAllWalls()) {
        LOG_DEBUG("Wall: {}", w.toString());
    }
    LOG_DEBUG("LEAVE with removed= {}", removed);

    return removed;
}

std::vector<Wall>
Building::SplitWall(const std::shared_ptr<SubRoom> & subroom, const Wall & bigWall) const
{
    std::vector<Wall> WallPieces;

    LOG_DEBUG("{} collect wall pieces with ", subroom->GetSubRoomID());
    LOG_DEBUG("Wall {}", bigWall.toString());

    auto walls       = subroom->GetAllWalls();
    auto crossings   = subroom->GetAllCrossings();
    auto transitions = subroom->GetAllTransitions();
    // TODO: Hlines too?
    std::vector<Line> walls_and_exits = std::vector<Line>();
    // TODO: check if this is GetAllGoals()?
    //  collect all crossings
    for(auto && cros : crossings)
        walls_and_exits.push_back(*cros);
    //collect all transitions
    for(auto && trans : transitions)
        walls_and_exits.push_back(*trans);
    for(auto && wall : walls)
        walls_and_exits.push_back(wall);

    for(auto const & other : walls_and_exits) {
        LOG_DEBUG("Other: {}", other.toString());
        if((bigWall == other) || (bigWall.ShareCommonPointWith(other)))
            continue;
        Point intersectionPoint;

        if(bigWall.IntersectionWith(other, intersectionPoint)) {
            if(intersectionPoint == bigWall.GetPoint1() || intersectionPoint == bigWall.GetPoint2())
                continue;

            LOG_DEBUG(
                "BigWall: {}, Other: {}, Intersection Point: {}",
                bigWall.toString(),
                other.toString(),
                intersectionPoint.toString());
            if(std::isnan(intersectionPoint._x) || std::isnan(intersectionPoint._y))
                continue;

            Wall NewWall(intersectionPoint, bigWall.GetPoint2());  // [IP -- P2]
            Wall NewWall2(bigWall.GetPoint1(), intersectionPoint); // [IP -- P2]

            WallPieces.push_back(NewWall);
            WallPieces.push_back(NewWall2);

            LOG_DEBUG("Added two new wall2: {} and {}", NewWall.toString(), NewWall2.toString());
        }
    } //other walls

    LOG_DEBUG("Leaving collect, WallPieces size: {}", WallPieces.size());

    return WallPieces;
}
bool Building::ReplaceBigWall(
    const std::shared_ptr<SubRoom> & subroom,
    const Wall & bigWall,
    std::vector<Wall> & WallPieces) const
{
    LOG_DEBUG(
        "Replacing big line in Room {} | Subroom {} with: {}",
        subroom->GetRoomID(),
        subroom->GetSubRoomID(),
        bigWall.toString());

    // REMOVE BigLINE
    LOG_DEBUG("s+ =", subroom->GetAllWalls().size());

    bool res = subroom->RemoveWall(bigWall);

    LOG_DEBUG("s- =", subroom->GetAllWalls().size());

    if(!res) {
        LOG_ERROR("Correct fails. Could not remove wall: {}", bigWall.toString());
        return false;
    }
    // ADD SOME LINE
    res = AddWallToSubroom(subroom, WallPieces);
    if(!res) {
        LOG_ERROR("Correct fails. Could not add new wall piece");
        return false;
    }

    return true;
}
const fs::path & Building::GetProjectFilename() const
{
    return _configuration->GetProjectFile();
}

const fs::path & Building::GetProjectRootDir() const
{
    return _configuration->GetProjectRootDir();
}

const fs::path & Building::GetGeometryFilename() const
{
    return _configuration->GetGeometryFile();
}

bool Building::AddWallToSubroom(
    const std::shared_ptr<SubRoom> & subroom,
    std::vector<Wall> WallPieces) const
{
    // CHOOSE WHICH PIECE SHOULD BE ADDED TO SUBROOM
    // this is a challngig function

    LOG_DEBUG("Entering AddWallToSubroom with following walls:");
    for(const auto & w : WallPieces)
        LOG_DEBUG("Wall: {}", w.toString());

    auto walls   = subroom->GetAllWalls();
    int maxCount = -1;
    Wall choosenWall;
    for(const auto & w : WallPieces) {
        int count = 0;
        for(const auto & checkWall : walls) {
            if(checkWall == w)
                continue; // don't count big wall
            if(w.ShareCommonPointWith(checkWall))
                count++;
            else { // first use the cheap ShareCommonPointWith() before entering
                   // this else
                Point interP;
                if(w.IntersectionWith(checkWall, interP)) {
                    if((!std::isnan(interP._x)) && (!std::isnan(interP._y)))
                        count++;
                }
            }
        }
        auto transitions = subroom->GetAllTransitions();
        auto crossings   = subroom->GetAllCrossings();
        //auto h = subroom.second->GetAllHlines();
        for(auto transition : transitions)
            if(w.ShareCommonPointWith(*transition))
                count++;
        for(auto crossing : crossings)
            if(w.ShareCommonPointWith(*crossing))
                count++;

        if(count >= 2) {
            subroom->AddWall(w);

            LOG_DEBUG("Wall candidate: {}", w.toString());

            if(count > maxCount) {
                maxCount    = count;
                choosenWall = w;
            }

            LOG_DEBUG("Count= {},  maxCount= {}", count, maxCount);
        }
    } // WallPieces
    if(maxCount < 0)
        return false;
    else {
        LOG_DEBUG("Choosen Wall: {}", choosenWall.toString());

        subroom->AddWall(choosenWall);
        return true;
    }
}

Room * Building::GetRoom(std::string caption) const
{
    for(const auto & it : _rooms) {
        if(it.second->GetCaption() == caption)
            return it.second.get();
    }
    LOG_ERROR("Room not found with caption {}", caption);
    exit(EXIT_FAILURE);
}

bool Building::AddCrossing(Crossing * line)
{
    int IDRoom     = line->GetRoom1()->GetID();
    int IDLine     = line->GetUniqueID();
    int IDCrossing = 1000 * IDRoom + IDLine;
    if(_crossings.count(IDCrossing) != 0) {
        LOG_ERROR("Duplicate index for crossing found [{}] in Routing::AddCrossing()", IDCrossing);
        exit(EXIT_FAILURE);
    }
    _crossings[IDCrossing] = line;
    return true;
}

bool Building::RemoveTransition(Transition * line)
{
    if(_transitions.count(line->GetID()) != 0) {
        _transitions.erase(line->GetID());
        return true;
    }
    return false;
}

bool Building::AddTransition(Transition * line)
{
    if(_transitions.count(line->GetID()) != 0) {
        LOG_ERROR(
            "Duplicate index for transition found [{}] in Routing::AddTransition()", line->GetID());
        exit(EXIT_FAILURE);
    }
    _transitions[line->GetID()] = line;

    return true;
}

bool Building::AddHline(Hline * line)
{
    if(_hLines.count(line->GetID()) != 0) {
        // check if the lines are identical
        Hline * ori = _hLines[line->GetID()];
        if(ori->operator==(*line)) {
            LOG_INFO("Skipping identical hlines with ID [{}]", line->GetID());
            return false;
        } else {
            LOG_ERROR(
                "Duplicate index for hlines found [{}] in Routing::AddHline(). You have "
                "[{}] hlines",
                line->GetID(),
                _hLines.size());
            exit(EXIT_FAILURE);
        }
    }
    _hLines[line->GetID()] = line;
    return true;
}

bool Building::AddGoal(Goal * goal)
{
    if(_goals.count(goal->GetId()) != 0) {
        LOG_ERROR("Duplicate index for goal found [{}] in Routing::AddGoal()", goal->GetId());
        exit(EXIT_FAILURE);
    }
    _goals[goal->GetId()] = goal;


    return true;
}

bool Building::AddTrainType(std::shared_ptr<TrainType> TT)
{
    if(_trainTypes.count(TT->type) != 0) {
        LOG_WARNING("Duplicate type for train found [{}]", TT->type);
    }
    _trainTypes[TT->type] = TT;
    return true;
}

bool Building::AddTrainTimeTable(std::shared_ptr<TrainTimeTable> TTT)
{
    if(_trainTimeTables.count(TTT->id) != 0) {
        LOG_WARNING("Duplicate id for train time table found [{}]", TTT->id);
        exit(EXIT_FAILURE);
    }
    _trainTimeTables[TTT->id] = TTT;
    return true;
}

const std::map<int, Crossing *> & Building::GetAllCrossings() const
{
    return _crossings;
}

const std::map<int, Transition *> & Building::GetAllTransitions() const
{
    return _transitions;
}

const std::map<int, Hline *> & Building::GetAllHlines() const
{
    return _hLines;
}

const std::map<std::string, std::shared_ptr<TrainType>> & Building::GetTrainTypes() const
{
    return _trainTypes;
}

const std::map<int, std::shared_ptr<TrainTimeTable>> & Building::GetTrainTimeTables() const
{
    return _trainTimeTables;
}

const std::map<int, std::shared_ptr<Platform>> & Building::GetPlatforms() const
{
    return _platforms;
}

bool Building::AddPlatform(std::shared_ptr<Platform> P)
{
    if(_platforms.count(P->id) != 0) {
        LOG_WARNING("Duplicate platform found [{}]", P->id);
    }
    _platforms[P->id] = P;
    return true;
}

const std::map<int, Goal *> & Building::GetAllGoals() const
{
    return _goals;
}

Transition * Building::GetTransition(std::string caption) const
{
    //eventually
    std::map<int, Transition *>::const_iterator itr;
    for(itr = _transitions.begin(); itr != _transitions.end(); ++itr) {
        if(itr->second->GetCaption() == caption)
            return itr->second;
    }

    LOG_WARNING("No Transition with Caption: {}", caption);
    exit(EXIT_FAILURE);
}

Transition * Building::GetTransition(int ID) const //ar.graf: added const 2015-12-10
{
    if(_transitions.count(ID) == 1) {
        return _transitions.at(ID);
    } else {
        if(ID == -1)
            return nullptr;
        else {
            LOG_ERROR(
                "I could not find any transition with the 'ID' [{}]. You have defined [{}] "
                "transitions",
                ID,
                _transitions.size());
            exit(EXIT_FAILURE);
        }
    }
}

Crossing * Building::GetCrossing(int ID) const
{
    if(_crossings.count(ID) == 1) {
        return _crossings.at(ID);
    } else {
        if(ID == -1)
            return nullptr;
        else {
            LOG_ERROR(
                "I could not find any crossing with the 'ID' [{}]. You have defined [{}] "
                "transitions",
                ID,
                _crossings.size());
            exit(EXIT_FAILURE);
        }
    }
}

Goal * Building::GetFinalGoal(int ID) const
{
    if(_goals.count(ID) == 1) {
        return _goals.at(ID);
    } else {
        if(ID == -1)
            return nullptr;
        else {
            LOG_ERROR(
                "I could not find any goal with the 'ID' [{}]. You have defined [{}] goals",
                ID,
                _goals.size());
            exit(EXIT_FAILURE);
        }
    }
}

Crossing * Building::GetTransOrCrossByName(std::string caption) const
{
    {
        //eventually
        std::map<int, Transition *>::const_iterator itr;
        for(itr = _transitions.begin(); itr != _transitions.end(); ++itr) {
            if(itr->second->GetCaption() == caption)
                return itr->second;
        }
    }
    {
        //finally the  crossings
        std::map<int, Crossing *>::const_iterator itr;
        for(itr = _crossings.begin(); itr != _crossings.end(); ++itr) {
            if(itr->second->GetCaption() == caption)
                return itr->second;
        }
    }

    LOG_WARNING("No Transition or Crossing with Caption: {}", caption);
    return nullptr;
}

Hline * Building::GetTransOrCrossByUID(int id) const
{
    {
        //eventually transitions
        std::map<int, Transition *>::const_iterator itr;
        for(itr = _transitions.begin(); itr != _transitions.end(); ++itr) {
            if(itr->second->GetUniqueID() == id)
                return itr->second;
        }
    }
    {
        //then the  crossings
        std::map<int, Crossing *>::const_iterator itr;
        for(itr = _crossings.begin(); itr != _crossings.end(); ++itr) {
            if(itr->second->GetUniqueID() == id)
                return itr->second;
        }
    }
    {
        //finally the  hlines
        for(auto itr = _hLines.begin(); itr != _hLines.end(); ++itr) {
            if(itr->second->GetUniqueID() == id)
                return itr->second;
        }
    }
    LOG_ERROR("No Transition, Crossing or hline with ID {} found.", id);
    return nullptr;
}

SubRoom * Building::GetSubRoomByUID(int uid) const
{
    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            if(itr_subroom.second->GetUID() == uid)
                return itr_subroom.second.get();
        }
    }
    LOG_ERROR("No subroom exits with the unique id {}", uid);
    return nullptr;
}

bool Building::IsVisible(
    const Point & p1,
    const Point & p2,
    const std::vector<SubRoom *> & subrooms,
    bool considerHlines)
{
    //loop over all subrooms if none is provided
    if(subrooms.empty()) {
        for(auto && itr_room : _rooms) {
            for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
                if(!itr_subroom.second->IsVisible(p1, p2, considerHlines))
                    return false;
            }
        }
    } else {
        for(auto && sub : subrooms) {
            if(sub && !sub->IsVisible(p1, p2, considerHlines))
                return false;
        }
    }

    return true;
}

bool Building::Triangulate()
{
    LOG_INFO("Triangulating the geometry.");
    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            if(!itr_subroom.second->Triangulate())
                return false;
        }
    }
    LOG_INFO("Done.");
    return true;
}

std::vector<Point> Building::GetBoundaryVertices() const
{
    double xMin = FLT_MAX;
    double yMin = FLT_MAX;
    double xMax = -FLT_MAX;
    double yMax = -FLT_MAX;
    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            const std::vector<Point> vertices = itr_subroom.second->GetPolygon();

            for(Point point : vertices) {
                if(point._x > xMax)
                    xMax = point._x;
                else if(point._x < xMin)
                    xMin = point._x;
                if(point._y > yMax)
                    yMax = point._y;
                else if(point._y < yMin)
                    yMin = point._y;
            }
        }
    }
    return std::vector<Point>{
        Point(xMin, yMin), Point(xMin, yMax), Point(xMax, yMax), Point(xMax, yMin)};
}

bool Building::SanityCheck()
{
    LOG_INFO("Checking the geometry for artifacts: (Ignore Warnings, if ff_[...] router is used!)");
    bool status = true;

    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            if(!itr_subroom.second->SanityCheck())
                status = false;
        }
    }

    LOG_INFO("...Done.");
    return status;
}

#ifdef _SIMULATOR

void Building::UpdateGrid()
{
    _linkedCellGrid->Update(_allPedestrians);
}

void Building::InitGrid()
{
    // first look for the geometry boundaries
    double x_min = FLT_MAX;
    double x_max = FLT_MIN;
    double y_min = FLT_MAX;
    double y_max = FLT_MIN;

    //finding the bounding of the grid
    // and collect the pedestrians
    for(auto && itr_room : _rooms) {
        for(auto && itr_subroom : itr_room.second->GetAllSubRooms()) {
            for(auto && wall : itr_subroom.second->GetAllWalls()) {
                double x1 = wall.GetPoint1()._x;
                double y1 = wall.GetPoint1()._y;
                double x2 = wall.GetPoint2()._x;
                double y2 = wall.GetPoint2()._y;

                double xmax = (x1 > x2) ? x1 : x2;
                double xmin = (x1 > x2) ? x2 : x1;
                double ymax = (y1 > y2) ? y1 : y2;
                double ymin = (y1 > y2) ? y2 : y1;

                x_min = (xmin <= x_min) ? xmin : x_min;
                x_max = (xmax >= x_max) ? xmax : x_max;
                y_max = (ymax >= y_max) ? ymax : y_max;
                y_min = (ymin <= y_min) ? ymin : y_min;
            }
        }
    }

    double cellSize = _configuration->GetLinkedCellSize();
    //make the grid slightly larger.
    x_min = x_min - 1 * cellSize;
    x_max = x_max + 1 * cellSize;
    y_min = y_min - 1 * cellSize;
    y_max = y_max + 1 * cellSize;

    double boundaries[4] = {x_min, x_max, y_min, y_max};

    //no algorithms
    // the domain is made of a single cell
    if(cellSize == -1) {
        LOG_INFO("Brute Force will be used for neighborhoods query");
        if((x_max - x_min) < (y_max - y_min)) {
            cellSize = (y_max - y_min);
        } else {
            cellSize = (x_max - x_min);
        }

    } else {
        LOG_INFO("Initializing the grid with cell size: {}.", cellSize);
    }

    //TODO: the number of pedestrian should be calculated using the capacity of the sources
    //int nped= Pedestrian::GetAgentsCreated() +  for src:sources  src->GetMaxAgents()

    _linkedCellGrid = new LCGrid(boundaries, cellSize, Pedestrian::GetAgentsCreated());
    _linkedCellGrid->ShallowCopy(_allPedestrians);

    LOG_INFO("Done with Initializing the grid");
}

void Building::DeletePedestrian(Pedestrian *& ped)
{
    std::vector<Pedestrian *>::iterator it;
    it = find(_allPedestrians.begin(), _allPedestrians.end(), ped);
    if(it == _allPedestrians.end()) {
        LOG_ERROR("Pedestrian with ID {} not found.", ped->GetID());
        return;
    } else {
        // save the path history for this pedestrian before removing from the simulation
        if(_savePathway) {
            // string results;
            std::string path = (*it)->GetPath();
            std::vector<std::string> brokenpaths;
            StringExplode(path, ">", &brokenpaths);
            for(unsigned int i = 0; i < brokenpaths.size(); i++) {
                std::vector<std::string> tags;
                StringExplode(brokenpaths[i], ":", &tags);
                std::string room  = _rooms[atoi(tags[0].c_str())]->GetCaption();
                std::string trans = GetTransition(atoi(tags[1].c_str()))->GetCaption();
                //ignore crossings/hlines
                if(trans != "")
                    _pathWayStream << room << " " << trans << std::endl;
            }
        }
    }
    _allPedestrians.erase(it);
    delete ped;
}

const std::vector<Pedestrian *> & Building::GetAllPedestrians() const
{
    return _allPedestrians;
}

void Building::AddPedestrian(Pedestrian * ped)
{
    for(unsigned int p = 0; p < _allPedestrians.size(); p++) {
        Pedestrian * ped1 = _allPedestrians[p];
        if(ped->GetID() == ped1->GetID()) {
            std::cout << "Pedestrian " << ped->GetID() << " already in the room." << std::endl;
            return;
        }
    }
    _allPedestrians.push_back(ped);
}

void Building::GetPedestrians(int room, int subroom, std::vector<Pedestrian *> & peds) const
{
    for(auto && ped : _allPedestrians) {
        if((room == ped->GetRoomID()) && (subroom == ped->GetSubRoomID())) {
            peds.push_back(ped);
        }
    }
}

//obsolete
void Building::InitSavePedPathway(const std::string & filename)
{
    _pathWayStream.open(filename.c_str());
    _savePathway = true;

    if(_pathWayStream.is_open()) {
        LOG_INFO("Saving pedestrian paths to [{}]", filename);
        _pathWayStream << "##pedestrian ways" << std::endl;
        _pathWayStream << "#nomenclature roomid  caption" << std::endl;
        _pathWayStream << "#data room exit_id" << std::endl;
    } else {
        LOG_INFO("Unable to open [{}]", filename);
        LOG_INFO("Writing to stdout instead.");
    }
}

void Building::StringExplode(
    std::string str,
    std::string separator,
    std::vector<std::string> * results)
{
    size_t found;
    found = str.find_first_of(separator);
    while(found != std::string::npos) {
        if(found > 0) {
            results->push_back(str.substr(0, found));
        }
        str   = str.substr(found + 1);
        found = str.find_first_of(separator);
    }
    if(str.length() > 0) {
        results->push_back(str);
    }
}

Pedestrian * Building::GetPedestrian(int pedID) const
{
    for(unsigned int p = 0; p < _allPedestrians.size(); p++) {
        Pedestrian * ped = _allPedestrians[p];
        if(ped->GetID() == pedID) {
            return ped;
        }
    }

    return nullptr;
}

Transition * Building::GetTransitionByUID(int uid) const
{
    for(auto && trans : _transitions) {
        if(trans.second->GetUniqueID() == uid)
            return trans.second;
    }
    return nullptr;
}

Crossing * Building::GetCrossingByUID(int uid) const
{
    for(auto && cross : _crossings) {
        if(cross.second->GetUniqueID() == uid)
            return cross.second;
    }
    return nullptr;
}

bool Building::SaveGeometry(const fs::path & filename) const
{
    std::stringstream geometry;

    //write the header
    geometry << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" << std::endl;
    geometry << "<geometry version=\"0.8\" caption=\"second life\" unit=\"m\"\n "
                " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n  "
                " xsi:noNamespaceSchemaLocation=\"http://134.94.2.137/jps_geoemtry.xsd\">"
             << std::endl
             << std::endl;

    //write the rooms
    geometry << "<rooms>" << std::endl;
    for(auto && itroom : _rooms) {
        auto && room = itroom.second;
        geometry << "\t<room id =\"" << room->GetID() << "\" caption =\"" << room->GetCaption()
                 << "\">" << std::endl;
        for(auto && itr_sub : room->GetAllSubRooms()) {
            auto && sub          = itr_sub.second;
            const double * plane = sub->GetPlaneEquation();
            geometry << "\t\t<subroom id =\"" << sub->GetSubRoomID() << "\" caption=\"dummy_caption"
                     << "\" class=\"" << sub->GetType() << "\" A_x=\"" << plane[0] << "\" B_y=\""
                     << plane[1] << "\" C_z=\"" << plane[2] << "\">" << std::endl;

            for(auto && wall : sub->GetAllWalls()) {
                const Point & p1 = wall.GetPoint1();
                const Point & p2 = wall.GetPoint2();

                geometry << "\t\t\t<polygon caption=\"wall\" type=\"" << wall.GetType() << "\">"
                         << std::endl
                         << "\t\t\t\t<vertex px=\"" << p1._x << "\" py=\"" << p1._y << "\"/>"
                         << std::endl
                         << "\t\t\t\t<vertex px=\"" << p2._x << "\" py=\"" << p2._y << "\"/>"
                         << std::endl
                         << "\t\t\t</polygon>" << std::endl;
            }

            if(sub->GetType() == "stair") {
                const Point & up   = ((Stair *) sub.get())->GetUp();
                const Point & down = ((Stair *) sub.get())->GetDown();
                geometry << "\t\t\t<up px=\"" << up._x << "\" py=\"" << up._y << "\"/>"
                         << std::endl;
                geometry << "\t\t\t<down px=\"" << down._x << "\" py=\"" << down._y << "\"/>"
                         << std::endl;
            }

            geometry << "\t\t</subroom>" << std::endl;
        }

        //write the crossings
        geometry << "\t\t<crossings>" << std::endl;
        for(auto const & mapcross : _crossings) {
            Crossing * cross = mapcross.second;

            //only write the crossings in this rooms
            if(cross->GetRoom1()->GetID() != room->GetID())
                continue;

            const Point & p1 = cross->GetPoint1();
            const Point & p2 = cross->GetPoint2();

            geometry << "\t<crossing id =\"" << cross->GetID() << "\" subroom1_id=\""
                     << cross->GetSubRoom1()->GetSubRoomID() << "\" subroom2_id=\""
                     << cross->GetSubRoom2()->GetSubRoomID() << "\">" << std::endl;

            geometry << "\t\t<vertex px=\"" << p1._x << "\" py=\"" << p1._y << "\"/>" << std::endl
                     << "\t\t<vertex px=\"" << p2._x << "\" py=\"" << p2._y << "\"/>" << std::endl
                     << "\t</crossing>" << std::endl;
        }
        geometry << "\t\t</crossings>" << std::endl;
        geometry << "\t</room>" << std::endl;
    }

    geometry << "</rooms>" << std::endl;

    //write the transitions
    geometry << "<transitions>" << std::endl;

    for(auto const & maptrans : _transitions) {
        Transition * trans = maptrans.second;
        const Point & p1   = trans->GetPoint1();
        const Point & p2   = trans->GetPoint2();
        int room2_id       = -1;
        int subroom2_id    = -1;
        if(trans->GetRoom2()) {
            room2_id    = trans->GetRoom2()->GetID();
            subroom2_id = trans->GetSubRoom2()->GetSubRoomID();
        }

        geometry << "\t<transition id =\"" << trans->GetID() << "\" caption=\""
                 << trans->GetCaption() << "\" type=\"" << trans->GetType() << "\" room1_id=\""
                 << trans->GetRoom1()->GetID() << "\" subroom1_id=\""
                 << trans->GetSubRoom1()->GetSubRoomID() << "\" room2_id=\"" << room2_id
                 << "\" subroom2_id=\"" << subroom2_id << "\">" << std::endl;

        geometry << "\t\t<vertex px=\"" << p1._x << "\" py=\"" << p1._y << "\"/>" << std::endl
                 << "\t\t<vertex px=\"" << p2._x << "\" py=\"" << p2._y << "\"/>" << std::endl
                 << "\t</transition>" << std::endl;
    }

    geometry << "</transitions>" << std::endl;
    geometry << "</geometry>" << std::endl;
    //write the routing file

    std::ofstream geofile(filename.string());
    if(geofile.is_open()) {
        geofile << geometry.str();
        LOG_INFO("File saved to {}", filename.string());
    } else {
        LOG_ERROR("Unable to save the geometry to {}.", filename.string());
        return false;
    }

    return true;
}

#endif // _SIMULATOR
