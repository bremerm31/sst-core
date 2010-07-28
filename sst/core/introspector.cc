// Copyright 2009-2010 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2010, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"

#include <boost/foreach.hpp>
#include <boost/mpi.hpp>

#include "sst/core/simulation.h"
#include "sst/core/introspector.h"
#include "sst/core/exit.h"
#include "sst/core/timeLord.h"
//#include "sst/core/syncEvent.h"


namespace SST {


Introspector::Introspector()
{
}

TimeConverter*
Introspector::registerClock( std::string freq, Clock::HandlerBase* handler)
{
    defaultTimeBase = Simulation::getSimulation()->registerClock(freq,handler);
    return defaultTimeBase;
}


std::list<IntrospectedComponent*> Introspector::getModels(const std::string CompType)
{
    const CompMap_t &CompMap = Simulation::getSimulation()->getComponentMap();

    for( CompMap_t::const_iterator iter = CompMap.begin();
    	                    iter != CompMap.end(); ++iter )
    {
	//printf("CompMap has %s with id = %lu\n", (*iter).second->type.c_str(), (*iter).second->Id());
        if (CompType.empty() == true)
	    MyCompList.push_back(dynamic_cast<IntrospectedComponent*>((*iter).second));   
	else if ( (*iter).second->type.compare(CompType) == 0){
	    //printf("Introspector will monitor ID %lu\n", (*iter).second->Id());
	    MyCompList.push_back(dynamic_cast<IntrospectedComponent*>((*iter).second));
	}
    }
    return MyCompList;
}

void Introspector::monitorComponent(IntrospectedComponent* c)
{
    c->addToIntroList(this);
}

void Introspector::addToIntDatabase(IntrospectedComponent* c, int dataID){
    //std::cout << "Introspector::addToIntDatabase added component " << c->Id() << "'s data with dataID = " << dataID << std::endl;
    DatabaseInt.insert(std::make_pair(c, dataID)); 
}
void Introspector::addToDoubleDatabase(IntrospectedComponent* c, int dataID){ 
    DatabaseDouble.insert(std::make_pair(c, dataID)); 
}

void Introspector::collectInt(collect_type ctype, uint64_t invalue, mpi_operation op, int rank){

    boost::mpi::communicator world; 

    switch(ctype)
    {
        case 0:  //gather
	    if (world.rank() == 0) {
    		 gather( world, invalue, arrayvalue, 0); 
  	    } else {
    		 gather(world, invalue, 0);
  	    }
	    break;	
	case 1:  //all gather
	    all_gather( world, invalue, arrayvalue);
	    break;	
	case 2:  //broadcast
	    if (world.rank() == rank)
	        value = invalue;
	    broadcast(world, value, rank);
	    break;	
	case 3:  //reduce
	    switch(op)
	    {
		case 0: //minimum
	            if (world.rank() == 0) {
    		        reduce( world, invalue, minvalue, boost::mpi::minimum<int>(), 0); 
    		        //std::cout << "The minimum value is " << minvalue << std::endl;
  	            } else {
    		        reduce(world, invalue, boost::mpi::minimum<int>(), 0);
  	            }
		    break;
		case 1: //maximum
	            if (world.rank() == 0) {
    		        reduce( world, invalue, maxvalue, boost::mpi::maximum<int>(), 0); 
    		        //std::cout << "The maximum value is " << maxvalue << std::endl;
  	            } else {
    		        reduce(world, invalue, boost::mpi::maximum<int>(), 0);
  	            }
		    break;
		case 2: //sum
		    if (world.rank() == 0) {
    		        reduce( world, invalue, value, std::plus<int>(), 0); 
    		        //std::cout << "The value is " << value << std::endl;
  	            } else {
    		        reduce(world, invalue, std::plus<int>(), 0);
  	            }
		    break;
		default:
		    break;
	    }
	    break;
	case 4:  //all reduce
	    switch(op)
	    {
		case 0: //minimum	            
    		        all_reduce( world, invalue, minvalue, boost::mpi::minimum<int>()); 
		    break;
		case 1: //maximum            
    		        all_reduce( world, invalue, maxvalue, boost::mpi::maximum<int>());       
		    break;
		case 2: //sum   
    		        all_reduce( world, invalue, value, std::plus<int>()); 
		    break;
		default:
		    break;
	    }
	    break;	
	default:
	    break;	
    }
}

// KSH:  SyncEvent is gone.  Introspector should create an action to do what syncevent was doing
void Introspector::oneTimeCollect(SimTime_t time, EventHandlerBase<bool,Event*>* functor){
//     Simulation *sim = Simulation::getSimulation();
//     SyncEvent* event = new SyncEvent();

//     sim->insertEvent( time, event, functor );

}


} //namespace SST
