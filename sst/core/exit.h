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


#ifndef SST_EXIT_H
#define SST_EXIT_H

#include <set>
#include <sst/core/sst.h>
#include <sst/core/action.h>

namespace SST{

class Simulation;
class TimeConverter;

class Exit : public Action {
public:
    // Exit needs to register a handler during constructor time, which
    // requires a simulation object.  But the simulation class creates
    // an Exit object during it's construction, meaning that
    // Simulation::getSimulation() won't work yet.  So Exit is the one
    // exception to the "constructors shouldn't take simulation
    // pointers" rule.  However, it still needs to follow the "classes
    // shouldn't contain pointers back to Simulation" rule.
    Exit( Simulation* sim, TimeConverter* period );

    bool refInc( ComponentId_t );
    bool refDec( ComponentId_t );

private:
    Exit() { } // for serialization only
    Exit(const Exit&);           // Don't implement
    void operator=(Exit const&); // Don't implement

//     bool handler( Event* );
    void execute(void);
    
//     EventHandler< Exit, bool, Event* >* m_functor;
    unsigned int    m_refCount;
    TimeConverter*  m_period;
    std::set<ComponentId_t> m_idSet;

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version )
    {
        ar & BOOST_SERIALIZATION_NVP(m_refCount);
        ar & BOOST_SERIALIZATION_NVP(m_period);
        ar & BOOST_SERIALIZATION_NVP(m_idSet);
    }
};

} // namespace SST

#endif // SST_EXIT_H
