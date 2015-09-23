/**
 * \file        AgentSource.cpp
 * \date        Apr 04, 2015
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
 * This class is responsible for materialising agent in a given location at a given frequency up to a maximum number.
 * The optimal position where to put the agents is given by various algorithms, for instance
 * the Voronoi algorithm or the Mitchell Best candidate algorithm.
 *
 **/

#include "AgentsSource.h"
#include "Pedestrian.h"
#include "StartDistribution.h"
#include "../IO/OutputHandler.h"

#include <iostream>

AgentsSource::AgentsSource(int id, const std::string& caption,int max_agents,int group_id,int frequency)
{
    _id=id;
    _caption=caption;
    _maxAgents=max_agents;
    _groupID=group_id;
    _frequency=frequency;
    _agentsGenerated=0;
    _agents.clear();
}

AgentsSource::~AgentsSource()
{
}

void AgentsSource::GenerateAgentsAndAddToPool(int count, Building* building)
{
     std::vector<Pedestrian*> peds;
     GenerateAgents(peds, count, building);
     _agents.insert(_agents.begin(),peds.begin(),peds.end());
     _agentsGenerated+=count;
}

void AgentsSource::RemoveAgentsFromPool(std::vector<Pedestrian*>& ped, int count)
{
     if((int)_agents.size()>=count)
     {
          ped.insert(ped.begin(),_agents.begin(),_agents.begin()+count);
          _agents.erase(_agents.begin(),_agents.begin()+count);
     }
     else
     {
          ped.insert(ped.begin(),_agents.begin(),_agents.end());
          _agents.clear();
     }
}

void AgentsSource::AddAgentsToPool(std::vector<Pedestrian*>& peds)
{
     _agents.insert(_agents.begin(),peds.begin(),peds.end());
}

int AgentsSource::GetPoolSize() const
{
     return _agents.size();
}

void AgentsSource::AddToPool(Pedestrian* ped)
{
     _agents.push_back(ped);
}

int AgentsSource::GetAgentsGenerated() const
{
     return _agentsGenerated;
}

void AgentsSource::SetAgentsGenerated(int agentsGenerated)
{
     _agentsGenerated = agentsGenerated;
}

const double* AgentsSource::GetBoundaries() const
{
     return _boundaries;
}

void AgentsSource::Setboundaries(double * bounds)
{
     _boundaries[0]=bounds[0];
     _boundaries[1]=bounds[1];
     _boundaries[2]=bounds[2];
     _boundaries[3]=bounds[3];
}

const std::string& AgentsSource::GetCaption() const
{
     return _caption;
}

int AgentsSource::GetFrequency() const
{
     return _frequency;
}

int AgentsSource::GetGroupId() const
{
     return _groupID;
}

int AgentsSource::GetId() const
{
     return _id;
}

int AgentsSource::GetMaxAgents() const
{
     return _maxAgents;
}

void AgentsSource::SetStartDistribution(std::shared_ptr<StartDistribution> startDistribution)
{
     _startDistribution=startDistribution;
}

int AgentsSource::GetGroupID() const
{
     return _groupID;
}

const std::shared_ptr<StartDistribution> AgentsSource::GetStartDistribution() const
{
     return _startDistribution;
}

void AgentsSource::GenerateAgents(std::vector<Pedestrian*>& peds, int count, Building* building)
{
     std::vector<Point> emptyPositions;
     int pid=Pedestrian::GetAgentsCreated(); // will be discarded
     for(int i=0;i<count;i++)
     {
          peds.push_back(_startDistribution->GenerateAgent(building, &pid,emptyPositions));
     }
}


void AgentsSource::Dump() const
{
     Log->Write("\n--------------------------");
     Log->Write("Dumping Source");
     Log->Write("ID: %d", _id);
     Log->Write("Group ID: %d", _groupID);
     Log->Write("Frequency: %d", _frequency);
     Log->Write("Agents Max: %d", _maxAgents);
     Log->Write("Agents Pool: %d", _agents.size());
     //getc(stdin);

}
