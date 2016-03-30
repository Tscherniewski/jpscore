#include "cognitivemap.h"
#include "../../../geometry/Point.h"
#include "../../../geometry/SubRoom.h"
#include "../../../geometry/Building.h"
#include "../../../pedestrian/Pedestrian.h"
#include "../../../visiLibity/source_code/visilibity.hpp"



CognitiveMap::CognitiveMap()
{

}


CognitiveMap::CognitiveMap(ptrBuilding b, ptrPed ped)
{
    _building=b;
    _ped=ped;

    _network = std::make_shared<GraphNetwork>(b,ped);

    std::string str(b->GetProjectRootDir()+"cogmap.xml");
    _outputhandler = std::make_shared<CogMapOutputHandler>(str.c_str());

    const double fps = UPDATE_RATE;
    _outputhandler->WriteToFileHeader(ped->GetID(),fps);
    _frame=0;

    _YAHPointer.SetPed(ped);
    _YAHPointer.SetPos(_ped->GetPos());

    _createdWayP=-1;

    //Destination and regions
    _currentRegion=nullptr;
    //Find maindestination in cogmapstorage

    _nextTarget=nullptr;

}

CognitiveMap::~CognitiveMap()
{

}

void CognitiveMap::UpdateMap()
{
    FindCurrentRegion();
    //AddAssociatedLandmarks(TriggerAssociations(LookForLandmarks()));
    SubRoom * sub_room = _building->GetRoom(_ped->GetRoomID())->GetSubRoom(_ped->GetSubRoomID());
    GraphVertex * vertex = (*_network->GetNavigationGraph())[sub_room];
    const GraphVertex::EdgesContainer edges = *(vertex->GetAllEdges());

    for (GraphEdge* edge:edges)
    {
        edge->SetFactor(1,"SpatialKnowledge");
    }

    // Has landmark been reached?
    CheckIfLandmarksReached();

}

void CognitiveMap::UpdateDirection()
{
    if (_ped->GetV()._x!=0 || _ped->GetV()._y!=0)
    {
        double angle = std::acos(_ped->GetV()._x/(std::sqrt(std::pow(_ped->GetV()._x,2)+std::pow(_ped->GetV()._y,2))));
        if (_ped->GetV()._y<0)
            angle=-angle;
        _YAHPointer.SetDirection(angle);
    }
}

void CognitiveMap::UpdateYAHPointer(const Point& move)
{
    _YAHPointer.UpdateYAH(move);
}

void CognitiveMap::AddRegions(Regions regions)
{
    _regions.swap(regions);
}

void CognitiveMap::AddRegion(ptrRegion region)
{
    _regions.push_back(region);
}

ptrRegion CognitiveMap::GetRegionByID(const int &regionID) const
{
    for (ptrRegion region:_regions)
    {
        if (region->GetId()==regionID)
        {
            return region;
        }
    }
    return nullptr;
}

void CognitiveMap::AddLandmarksSC(std::vector<ptrLandmark> landmarks)
{
    for (ptrLandmark landmark:landmarks)
    {
        if (std::find(_landmarksSubConcious.begin(), _landmarksSubConcious.end(), landmark)!=_landmarksSubConcious.end())
        {
            continue;
        }
        else
        {
            _landmarksSubConcious.push_back(landmark);

        }
    }
}

void CognitiveMap::AddLandmarks(std::vector<ptrLandmark> landmarks)
{
//    for (ptrLandmark landmark:landmarks)
//    {
//        if (std::find(_landmarks.begin(), _landmarks.end(), landmark)!=_landmarks.end())
//        {
//            continue;
//        }
//        else
//        {
//            _landmarks.push_back(landmark);
//            _landmarks.back()->SetPosInMap(_landmarks.back()->GetPosInMap()-_YAHPointer.GetPosDiff());


//        }
//    }
}

void CognitiveMap::AddLandmarkInRegion(ptrLandmark landmark, ptrRegion region)
{
    region->AddLandmark(landmark);
}

std::vector<ptrLandmark> CognitiveMap::LookForLandmarks()
{
//    SubRoom * sub_room = _building->GetRoom(_ped->GetRoomID())->GetSubRoom(_ped->GetSubRoomID());

//    std::vector<ptrLandmark> landmarks_found;

//    for (ptrLandmark landmark:_landmarksSubConcious)
//    {
//        if (landmark->GetRoom()==sub_room && std::find(_landmarks.begin(), _landmarks.end(), landmark)==_landmarks.end())
//        {
//           landmarks_found.push_back(landmark);
//           LandmarkReached(landmark);
//        }
//    }

//    AddLandmarks(landmarks_found);

//    return landmarks_found;
}

Landmarks CognitiveMap::TriggerAssociations(const std::vector<ptrLandmark> &landmarks)
{
    Landmarks associatedlandmarks;
    for (ptrLandmark landmark:landmarks)
    {
        Associations associations = landmark->GetAssociations();
        for (ptrAssociation association:associations)
        {
            if (association->GetLandmarkAssociation(landmark)!=nullptr)
            {
                associatedlandmarks.push_back(association->GetLandmarkAssociation(landmark));
                //pos-yahpointer diff
                associatedlandmarks.back()->SetPosInMap(landmarks.back()->GetPosInMap()-_YAHPointer.GetPosDiff());
            }
            if (association->GetConnectionAssoziation()!=nullptr)
            {
                //AddConnection(association->GetConnectionAssoziation());
            }
        }
    }
    return associatedlandmarks;
}

void CognitiveMap::AddAssociatedLandmarks(Landmarks landmarks)
{
//    for (ptrLandmark landmark:_waypContainer)
//    {
//        landmark->SetPriority(landmark->GetPriority()+landmarks.size());
//    }
//    int n=0;
//    for (ptrLandmark landmark:landmarks)
//    {
//        if (std::find(_waypContainer.begin(), _waypContainer.end(), landmark)!=_waypContainer.end())
//        {
//            continue;
//        }
//        landmark->SetPriority(n);
//        _waypContainer.push_back(landmark);
//        //Add as new target
//        if (!landmark->Visited())
//            _waypContainerTargetsSorted.push(landmark);
//        //Add as already visited
//        else
//        {
//            LandmarkReached(landmark);
//        }

//        n++;

//    }

}

void CognitiveMap::AssessDoors()
{
    SubRoom * sub_room = _building->GetRoom(_ped->GetRoomID())->GetSubRoom(_ped->GetSubRoomID());
    GraphVertex * vertex = (*_network->GetNavigationGraph())[sub_room];
    const GraphVertex::EdgesContainer edges = *(vertex->GetAllEdges());

    //const Point rpLandmark=_waypContainer[0]->GetPos();

    if (_nextTarget!=nullptr)
    {
        std::vector<GraphEdge* > sortedEdges = SortConShortestPath(_nextTarget,edges);
        //Log->Write(std::to_string(nextDoor->GetCrossing()->GetID()));

        for (unsigned int i=0; i<sortedEdges.size(); ++i)
        {
            sortedEdges[i]->SetFactor(0.6+0.1*i,"SpatialKnowledge");
//            Log->Write("INFO:\t "+std::to_string(sortedEdges[i]->GetCrossing()->GetID()));
//            Log->Write("INFO:\t "+std::to_string(sortedEdges[i]->GetFactor()));
        }

        //Log->Write(std::to_string(nextDoor->GetCrossing()->GetID()));
        Log->Write("INFO: Door assessed!");
    }

}

std::vector<GraphEdge *> CognitiveMap::SortConShortestPath(ptrLandmark landmark, const GraphVertex::EdgesContainer edges)
{

    std::list<double> sortedPathLengths;
    sortedPathLengths.push_back(ShortestPathDistance((*edges.begin()),landmark));
    std::list<GraphEdge* > sortedEdges;
    sortedEdges.push_back((*edges.begin()));

    auto itsortedEdges = sortedEdges.begin();
    //auto it = edges.begin();
    //++it;
    //starting at the second element
    for (auto it=edges.begin()++; it!=edges.end(); ++it)
    {
        double pathLengthDoorWayP = ShortestPathDistance((*it),landmark);
        Log->Write(std::to_string((*it)->GetCrossing()->GetID()));
        Log->Write(std::to_string(pathLengthDoorWayP));

        //Point vectorPathPedDoor = (*it)->GetCrossing()->GetCentre()-_ped->GetPos();
        itsortedEdges = sortedEdges.begin();
        //double pathLength = pathLengthDoorWayP+std::sqrt(std::pow(vectorPathPedDoor.GetX(),2)+std::pow(vectorPathPedDoor.GetY(),2));
        bool inserted=false;
        for (auto itLengths=sortedPathLengths.begin(); itLengths!=sortedPathLengths.end(); ++itLengths)
        {
            if (pathLengthDoorWayP >= *itLengths)
            {
                ++itsortedEdges;
                continue;
            }

            sortedPathLengths.insert(itLengths,pathLengthDoorWayP);
            sortedEdges.insert(itsortedEdges,(*it));
            inserted=true;
            break;
        }
        if (!inserted)
        {
            sortedPathLengths.push_back(pathLengthDoorWayP);
            sortedEdges.push_back((*it));
        }
    }

    Log->Write("subroom:\t");
    Log->Write(std::to_string(_ped->GetSubRoomID()));
    Log->Write("edgeOnShortest:");
    Log->Write(std::to_string(sortedEdges.front()->GetCrossing()->GetID()));
    std::vector<GraphEdge* > vectorSortedEdges;
    for (GraphEdge* edge:sortedEdges)
    {
        vectorSortedEdges.push_back(edge);
        //Log->Write("INFO:\t "+std::to_string(edge->GetCrossing()->GetID()));

    }
    //Log->Write("INFO:\t Next");
    return vectorSortedEdges;

}

//bool CognitiveMap::IsAroundLandmark(const Landmark& landmark, GraphEdge *edge) const
//{
//    Triangle triangle(_ped->GetPos(),landmark);
//    Point point(edge->GetCrossing()->GetCentre());
//    return triangle.Contains(point);
//}

ptrGraphNetwork CognitiveMap::GetGraphNetwork() const
{
    return _network;
}

double CognitiveMap::ShortestPathDistance(const GraphEdge* edge, const ptrLandmark landmark)
{

    SubRoom* sub_room = _building->GetRoom(_ped->GetRoomID())->GetSubRoom(_ped->GetSubRoomID());
    std::vector<Point> points = sub_room->GetPolygon();
    VisiLibity::Polygon boundary=VisiLibity::Polygon();
    VisiLibity::Polygon room=VisiLibity::Polygon();
    for (Point point:points)
    {
       room.push_back(VisiLibity::Point(point._x,point._y));
    }

//    for (int i=0; i<room.n();++i)
//    {
//        Log->Write("RoomVertices:");
//        std::cout << std::to_string(room[i].x()) << "\t" << std::to_string(room[i].y()) << std::endl;
//    }

    boundary.push_back(VisiLibity::Point(-100,-100));
    boundary.push_back(VisiLibity::Point(100,-100));
    boundary.push_back(VisiLibity::Point(100,100));
    boundary.push_back(VisiLibity::Point(-100,100));

    std::vector<VisiLibity::Polygon> polygons;
    polygons.push_back(boundary);
    polygons.push_back(room);

    VisiLibity::Environment environment(polygons);
    //environment.reverse_holes();

    VisiLibity::Point edgeP(edge->GetCrossing()->GetCentre()._x,edge->GetCrossing()->GetCentre()._y);
    Point pointOnShortestRoute = landmark->PointOnShortestRoute(edge->GetCrossing()->GetCentre());
    //Log->Write(std::to_string(pointOnShortestRoute.GetX())+" "+std::to_string(pointOnShortestRoute.GetY()));
    VisiLibity::Point wayP(pointOnShortestRoute._x,pointOnShortestRoute._y);//,landmark->GetPos().GetY());

    VisiLibity::Polyline polyline=environment.shortest_path(edgeP,wayP,0.01);
//    for (int i=0; i<polyline.size();++i)
//    {
//        Log->Write("Polyline:");
//        std::cout << std::to_string(polyline[i].x()) << "\t" << std::to_string(polyline[i].y()) << std::endl;
//    }
//    Log->Write("ShortestPathLength");
//    Log->Write(std::to_string(polyline.length()));

    return polyline.length();
}

const Point &CognitiveMap::GetOwnPos()
{
    return _YAHPointer.GetPos();
}

void CognitiveMap::WriteToFile()
{
//    ++_frame;
//    string data;
//    char tmp[CLENGTH] = "";

//    sprintf(tmp, "<frame ID=\"%d\">\n", _frame);
//    data.append(tmp);


//    for (ptrLandmark landmark:_landmarks)
//    {
//        char tmp1[CLENGTH] = "";
//        sprintf(tmp1, "<landmark ID=\"%d\"\t"
//               "caption=\"%s\"\t"
//               "x=\"%.6f\"\ty=\"%.6f\"\t"
//               "z=\"%.6f\"\t"
//               "rA=\"%.2f\"\trB=\"%.2f\"/>\n",
//               landmark->GetId(), landmark->GetCaption().c_str(), landmark->GetPosInMap().GetX(),
//               landmark->GetPosInMap().GetY(),0.0 ,landmark->GetA(), landmark->GetB());

//        data.append(tmp1);
//    }
//    bool current;

//    for (ptrLandmark landmark:_waypContainer)
//    {
//        current=false;
//        if (!_waypContainerTargetsSorted.empty())
//        {
//            if (landmark==_waypContainerTargetsSorted.top())
//              f  current=true;
//        }


//        char tmp2[CLENGTH] = "";
//        sprintf(tmp2, "<Landmark ID=\"%d\"\t"
//               "caption=\"%s\"\t"
//               "x=\"%.6f\"\ty=\"%.6f\"\t"
//               "z=\"%.6f\"\t"
//               "rA=\"%.2f\"\trB=\"%.2f\"\tcurrent=\"%i\"/>\n",
//               landmark->GetId(),landmark->GetCaption().c_str(), landmark->GetPosInMap().GetX(),
//               landmark->GetPosInMap().GetY(),0.0 ,landmark->GetA(), landmark->GetB(),
//               current);
//        data.append(tmp2);
//    }

//    char tmp3[CLENGTH]="";
//    sprintf(tmp3, "<YAHPointer "
//           "x=\"%.6f\"\ty=\"%.6f\"\t"
//           "z=\"%.6f\"\t"
//           "dir=\"%.2f\"/>\n",
//           _YAHPointer.GetPos().GetX(),
//           _YAHPointer.GetPos().GetY(),0.0 ,_YAHPointer.GetDirection());

//    data.append(tmp3);


////    for (ptrConnection connection:_connections)
////    {
////        char tmp4[CLENGTH]="";
////        sprintf(tmp4, "<connection "
////               "Landmark_landmarkID1=\"%d\"\tLandmark_landmarkID2=\"%d\"/>\n",
////               connection->GetLandmarks().first->GetId(),
////               connection->GetLandmarks().second->GetId());

////        data.append(tmp4);
////    }


//    data.append("</frame>\n");
//    _outputhandler->WriteToFile(data);
}

void CognitiveMap::SetNewLandmark()
{
    double a= 2.0;
    double b=2.0;
    ptrLandmark wayP = std::make_shared<Landmark>(_YAHPointer.GetPos(),a,b,_createdWayP);
    _createdWayP--;
    wayP->SetVisited(true);
    std::vector<ptrLandmark> vec;
    vec.push_back(wayP);
    AddLandmarks(vec);
}

Landmarks CognitiveMap::GetLandmarksConnectedWith(const ptrLandmark &landmark) const
{
    ptrRegion cRegion = GetRegionContaining(landmark);

    if (cRegion!=nullptr)
    {
        return cRegion->ConnectedWith(landmark);
    }
    else
    {
        return Landmarks();
    }
}

const ptrRegion CognitiveMap::GetRegionContaining(const ptrLandmark &landmark) const
{
    for (ptrRegion region:_regions)
    {
        if (region->ContainsLandmark(landmark))
            return region;
    }
    return nullptr;
}

void CognitiveMap::FindCurrentRegion()
{
    //for test purposes. has to be changed
    _currentRegion=_regions[0];
}

void CognitiveMap::CheckIfLandmarksReached()
{
    SubRoom * sub_room = _building->GetRoom(_ped->GetRoomID())->GetSubRoom(_ped->GetSubRoomID());

    for (ptrLandmark landmark:_currentRegion->GetLandmarks())
    {
        if (landmark->GetRoom()==sub_room)
        {
            std::string str1 = landmark->GetCaption()+" has been reached.";
            Log->Write(str1);
            _landmarksRecentlyVisited.push_back(landmark);
        }
    }
}

const ptrLandmark CognitiveMap::FindConnectionPoint(const ptrRegion &currentRegion, const ptrRegion &targetRegion) const
{
    for (ptrLandmark landmarka:currentRegion->GetLandmarks())
    {
        for (ptrLandmark landmarkb:targetRegion->GetLandmarks())
        {
            if (landmarka==landmarkb)
            {
                return landmarka;
            }
        }
    }

    return nullptr;

}

void CognitiveMap::FindMainDestination()
{
    for (ptrRegion region:_regions)
    {
        for (ptrLandmark landmark:region->GetLandmarks())
        {
            if (landmark->GetType()=="main")
            {
                _mainDestination=landmark;
                _targetRegion=region;
                return;
            }
        }
    }
    // if no destination was found
    _mainDestination=nullptr;
    _targetRegion=nullptr;
    return;
}

void CognitiveMap::FindNextTarget()
{

    _nextTarget=nullptr;
    // if not already in the region of the maindestination
    if (_targetRegion!=_currentRegion)
    {
        _nextTarget=FindConnectionPoint(_currentRegion,_targetRegion);
        // if connection point does not exist: Path searching to region
        if (_nextTarget==nullptr)
        {
            //Region is target
            _nextTarget=_targetRegion->GetRegionAsLandmark();
            return;
        }

    }
    else //destination is in current region
    {
       _nextTarget=_mainDestination;
    }

    // Function considers that nearLandmark can be the target itself if no nearer was found.
    ptrLandmark nearLandmark = FindNearLandmarkConnectedToTarget(_nextTarget);

    _nextTarget=nearLandmark;


    //if (_nextTarget!=nullptr)
    Log->Write(_nextTarget->GetCaption());
}

const ptrLandmark CognitiveMap::FindNearLandmarkConnectedToTarget(const ptrLandmark &target)
{

    Landmarks landmarksConnectedToTarget = FindLandmarksConnectedToTarget(target);

    //if target has no connections return nullptr
    if (landmarksConnectedToTarget.empty())
        return target;

    //ptrLandmark nearest = nullptr;
    //look for nearest located landmark

    //look for landmarks within a circle with the radius searchlimit
    // if no landmarks were found radius will be enlarged
    // if radius = distance(Pos->target) return target

    Point vector=target->GetPosInMap()-_YAHPointer.GetPos();
    double distanceToTarget=vector.Norm();
    int divisor = 24;
    double searchlimit=distanceToTarget/divisor;
    Landmarks nearLandmarks;

    while (searchlimit<distanceToTarget && nearLandmarks.empty())
    {
        for (ptrLandmark landmark:landmarksConnectedToTarget)
        {
            vector=landmark->GetPosInMap()-_YAHPointer.GetPos();
            double distance = vector.Norm();
            if (distance<=searchlimit)
            {
                nearLandmarks.push_back(landmark);
            }
        }
        searchlimit+=searchlimit;

    }

    if (nearLandmarks.empty())
        return target;

    // select best route to target from one of the nearLandmarks

    return FindBestRouteFromOneOf(nearLandmarks);


}

Landmarks CognitiveMap::FindLandmarksConnectedToTarget(const ptrLandmark &target)
{

    Landmarks connectedLandmarks;

    // landmarks directly connected to target
    Landmarks firstCandidates = GetLandmarksConnectedWith(target);

    for (ptrLandmark candidate:firstCandidates)
    {
        if(std::find(_landmarksRecentlyVisited.begin(), _landmarksRecentlyVisited.end(), candidate) == _landmarksRecentlyVisited.end())
        {
            connectedLandmarks.push_back(candidate);
        }
    }



    //Landmarks connected to landmarks connected to target
    Landmarks furtherCandidates;

    for (auto i=0; i<connectedLandmarks.size(); ++i)
    {
        furtherCandidates=GetLandmarksConnectedWith(connectedLandmarks[i]);

        for (ptrLandmark candidate : furtherCandidates)
        {
            // if candidate not already taken into account, not visited before or target itself
            if(std::find(connectedLandmarks.begin(), connectedLandmarks.end(), candidate) == connectedLandmarks.end()
                    && std::find(_landmarksRecentlyVisited.begin(), _landmarksRecentlyVisited.end(), candidate) == _landmarksRecentlyVisited.end()
                    && candidate!=target)
            {
                connectedLandmarks.push_back(candidate);

            }
        }
    }
    return connectedLandmarks;
}

const ptrLandmark CognitiveMap::FindBestRouteFromOneOf(const Landmarks &nearLandmarks)
{
    ptrLandmark bestChoice = nullptr;
    double minDistance = FLT_MAX;
    double cDistance;
    for (ptrLandmark landmark:nearLandmarks)
    {
        cDistance=_currentRegion->PathLengthFromLandmarkToTarget(landmark, _nextTarget);
        //Log->Write(landmark->GetCaption());
        //Log->Write(std::to_string(cDistance));
        if (cDistance<minDistance)
        {
            minDistance=cDistance;

            bestChoice=landmark;
        }
    }
    return bestChoice;

}

void CognitiveMap::InitLandmarkNetworksInRegions()
{
    for (ptrRegion region:_regions)
    {
        region->InitLandmarkNetwork();
    }
}




