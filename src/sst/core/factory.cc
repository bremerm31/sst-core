// Copyright 2009-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2016, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#include "sst_config.h"
#include "sst/core/factory.h"

#include <set>
#include <tuple>
#include <stdio.h>

#include "sst/core/simulation.h"
#include "sst/core/component.h"
#include "sst/core/element.h"
#include "sst/core/params.h"
#include "sst/core/linkMap.h"

// Statistic Output Objects
#include <sst/core/statapi/statoutputconsole.h>
#include <sst/core/statapi/statoutputtxt.h>
#include <sst/core/statapi/statoutputcsv.h>
#ifdef HAVE_HDF5
#include <sst/core/statapi/statoutputhdf5.h>
#endif

#ifdef HAVE_LIBZ
#include <sst/core/statapi/statoutputcsvgz.h>
#include <sst/core/statapi/statoutputtxtgz.h>
#endif

using namespace SST::Statistics;

namespace SST {

Factory* Factory::instance = NULL;

Factory::Factory(std::string searchPaths) :
    searchPaths(searchPaths),
    out(Output::getDefaultObject())
{
    if ( instance ) out.fatal(CALL_INFO, -1, "Already initialized a factory.\n");
    instance = this;
    loader = new ElemLoader(searchPaths);
}


Factory::~Factory()
{
    delete loader;
}



static bool checkPort(const std::string &def, const std::string &offered)
{
    const char * x = def.c_str();
    const char * y = offered.c_str();

    /* Special case.  Name of '*' matches everything */
    if ( *x == '*' && *(x+1) == '\0' ) return true;

    do {
        if ( *x == '%' && (*(x+1) == '(' || *(x+1) == 'd') ) {
            // We have a %d or %(var)d to eat
            x++;
            if ( *x == '(' ) {
                while ( *x && (*x != ')') ) x++;
                x++;  /* *x should now == 'd' */
            }
            if ( *x != 'd') /* Malformed string.  Fail all the things */
                return false;
            x++; /* Finish eating the variable */
            /* Now, eat the corresponding digits of y */
            while ( *y && isdigit(*y) ) y++;
        }
        if ( *x != *y ) return false;
        if ( *x == '\0' ) return true;
        x++;
        y++;
    } while ( *x && *y );
    if ( *x != *y ) return false; // aka, both NULL
    return true;
}

bool Factory::isPortNameValid(const std::string &type, const std::string port_name)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(type);
    // ensure library is already loaded...
    if (loaded_libraries.find(elemlib) == loaded_libraries.end()) {
        findLibrary(elemlib);
    }

    // now look for component
    std::string tmp = elemlib + "." + elem;

    std::vector<std::string> *portNames = NULL;

    eic_map_t::iterator eii = found_components.find(tmp);
    eis_map_t::iterator esii = found_subcomponents.find(tmp);
    if ( eii != found_components.end() ) {
        portNames = &eii->second.ports;
    } else if ( esii != found_subcomponents.end() ) {
        portNames = &esii->second.ports;
    }

    if ( portNames == NULL ) {
        out.fatal(CALL_INFO, -1,"can't find requested component or subcomponent %s.\n ", tmp.c_str());
        return false;
    }

    for ( auto p : *portNames ) {
        if ( checkPort(p, port_name) )
            return true;
    }
    return false;
}


Component*
Factory::CreateComponent(ComponentId_t id, 
                         std::string &type, 
                         Params& params)
{
    std::string elemlib, elem;
    
    std::tie(elemlib, elem) = parseLoadName(type);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // now look for component
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eic_map_t::iterator eii = found_components.find(tmp);
    if (eii == found_components.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested component %s.\n ", tmp.c_str());
        return NULL;
    }

    const ComponentInfo ci = eii->second;

    LinkMap *lm = Simulation::getSimulation()->getComponentLinkMap(id);
    lm->setAllowedPorts(&ci.ports);
    // lm->setAllowedPorts(GetComponentAllowedPorts(type));

    loadingComponentType = type;    
    params.pushAllowedKeys(ci.params);
    Component *ret = ci.component->alloc(id, params);
    params.popAllowedKeys();
    loadingComponentType = "";

    // if (NULL == ret) return ret;

    return ret;
}

StatisticOutput* 
Factory::CreateStatisticOutput(const std::string& statOutputType, const Params& statOutputParams)
{
    Module*           tempModule;
    StatisticOutput*  rtnStatOut = NULL;
    
    // Load the Statistic Output as a module first;  This allows 
    // us to provide StatisticOutputs as part of a element
    tempModule = CreateModule(statOutputType, const_cast<Params&>(statOutputParams));
    if (NULL != tempModule) {
        // Dynamic Cast the Module into a Statistic Output, if the module is not
        // a StatisticOutput, then return NULL
        rtnStatOut = dynamic_cast<StatisticOutput*>(tempModule);
    }
    
    return rtnStatOut;
}

bool 
Factory::DoesComponentInfoStatisticNameExist(const std::string& type, const std::string& statisticName)
{
    std::string compTypeToLoad = type;
    if (true == type.empty()) { 
        compTypeToLoad = loadingComponentType;
    }
    
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(compTypeToLoad);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // now look for component
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eic_map_t::iterator eii = found_components.find(tmp);
    if (eii == found_components.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested component %s.\n ", tmp.c_str());
        return false;
    }

    const ComponentInfo ci = eii->second;

    // See if the statistic exists
    for (uint32_t x = 0; x <  ci.statNames.size(); x++) {
        if (statisticName == ci.statNames[x]) {
            return true;
        }
    }
    return false;
}

bool 
Factory::DoesSubComponentInfoStatisticNameExist(const std::string& type, const std::string& statisticName)
{
    std::string compTypeToLoad = type;
    if (true == type.empty()) { 
        compTypeToLoad = loadingComponentType;
    }
    
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(compTypeToLoad);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // now look for subcomponent
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eis_map_t::iterator eii = found_subcomponents.find(tmp);
    if (eii == found_subcomponents.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested subcomponent %s.\n ", tmp.c_str());
        return false;
    }

    const SubComponentInfo ci = eii->second;

    // See if the statistic exists
    for (uint32_t x = 0; x <  ci.statNames.size(); x++) {
        if (statisticName == ci.statNames[x]) {
            return true;
        }
    }
    return false;
}

uint8_t 
Factory::GetComponentInfoStatisticEnableLevel(const std::string& type, const std::string& statisticName)
{
    std::string compTypeToLoad = type;
    if (true == type.empty()) { 
        compTypeToLoad = loadingComponentType;
    }
    
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(compTypeToLoad);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // now look for component
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eic_map_t::iterator eii = found_components.find(tmp);
    if (eii == found_components.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested component %s.\n ", tmp.c_str());
        return 0;
    }

    const ComponentInfo ci = eii->second;

    // See if the statistic exists, if so return the enable level
    for (uint32_t x = 0; x <  ci.statNames.size(); x++) {
        if (statisticName == ci.statNames[x]) {
            return ci.statEnableLevels[x];
        }
    }
    return 0;
}

std::string 
Factory::GetComponentInfoStatisticUnits(const std::string& type, const std::string& statisticName)
{
    std::string compTypeToLoad = type;
    if (true == type.empty()) { 
        compTypeToLoad = loadingComponentType;
    }
    
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(compTypeToLoad);

    // ensure library is already loaded...
    if (loaded_libraries.find(elemlib) == loaded_libraries.end()) {
        findLibrary(elemlib);
    }

    // now look for component
    std::string tmp = elemlib + "." + elem;
    eic_map_t::iterator eii = found_components.find(tmp);
    if (eii == found_components.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested component %s.\n ", tmp.c_str());
        return 0;
    }

    const ComponentInfo ci = eii->second;

    // See if the statistic exists, if so return the enable level
    for (uint32_t x = 0; x <  ci.statNames.size(); x++) {
        if (statisticName == ci.statNames[x]) {
            return ci.statUnits[x];
        }
    }
    return 0;
}


Module*
Factory::CreateModule(std::string type, Params& params)
{
    if("" == type) {
        Simulation::getSimulation()->getSimulationOutput().fatal(CALL_INFO,
                -1, "Error: Core attempted to load an empty module name, did you miss a module string in your input deck?\n");
    }

    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(type);

    if("sst" == elemlib) {
        return CreateCoreModule(elem, params);
    } else {
        // ensure library is already loaded...
        requireLibrary(elemlib);

        // now look for module
        std::string tmp = elemlib + "." + elem;

        std::lock_guard<std::recursive_mutex> lock(factoryMutex);
        eim_map_t::iterator eim = found_modules.find(tmp);
        if (eim == found_modules.end()) {
            out.fatal(CALL_INFO, -1, "can't find requested module %s.\n ", tmp.c_str());
            return NULL;
        }

        const ModuleInfo mi = eim->second;

        params.pushAllowedKeys(mi.params);
        Module *ret = mi.module->alloc(params);
        params.popAllowedKeys();
        return ret;
    }
}

Module* 
Factory::LoadCoreModule_StatisticOutputs(std::string& type, Params& params)
{
    // Names of sst.xxx Statistic Output Modules
    if (0 == ::strcasecmp("statoutputcsv", type.c_str())) {
        return new StatisticOutputCSV(params);
    }

    if (0 == ::strcasecmp("statoutputcsvgz", type.c_str())) {
#ifdef HAVE_LIBZ
	return new StatisticOutputCompressedCSV(params);
#else
	out.fatal(CALL_INFO, -1, "Statistics output requested compressed CSV but SST does not have LIBZ compiled.\n");
#endif
    }

    if (0 == ::strcasecmp("statoutputtxtgz", type.c_str())) {
#ifdef HAVE_LIBZ
	return new StatisticOutputCompressedTxt(params);
#else
	out.fatal(CALL_INFO, -1, "Statistics output requested compressed TXT but SST does not have LIBZ compiled.\n");
#endif
    }

#ifdef HAVE_HDF5
    if (0 == ::strcasecmp("statoutputhdf5", type.c_str())) {
        return new StatisticOutputHDF5(params);
    }
#endif

    if (0 == ::strcasecmp("statoutputtxt", type.c_str())) {
        return new StatisticOutputTxt(params);
    }

    if (0 == ::strcasecmp("statoutputconsole", type.c_str())) {
        return new StatisticOutputConsole(params);
    }


    return NULL;
}

Module*
Factory::CreateCoreModule(std::string type, Params& params) {
    // Try to load the core modules    
    Module* rtnModule = NULL;

    // Check each type of Core Module to load them
    if (NULL == rtnModule) {
        rtnModule = LoadCoreModule_StatisticOutputs(type, params);
    }
    
    // Try to load other core modules here...
    
    // Did we succeed in loading a core module? 
    if (NULL == rtnModule) {
        out.fatal(CALL_INFO, -1, "can't find requested core module %s\n", type.c_str());
    }
    return rtnModule;
}

Module*
Factory::CreateCoreModuleWithComponent(std::string type, Component* comp, Params& params) {
    out.fatal(CALL_INFO, -1, "can't find requested core module %s when loading with component\n", type.c_str());
    return NULL;
}


Module*
Factory::CreateModuleWithComponent(std::string type, Component* comp, Params& params)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(type);

    if("sst" == elemlib) {
        return CreateCoreModuleWithComponent(elem, comp, params);
    } else {

        // ensure library is already loaded...
        requireLibrary(elemlib);

        // now look for module
        std::string tmp = elemlib + "." + elem;

        std::lock_guard<std::recursive_mutex> lock(factoryMutex);

        eim_map_t::iterator eim = found_modules.find(tmp);
        if (eim == found_modules.end()) {
            out.fatal(CALL_INFO, -1,"can't find requested module %s.\n ", tmp.c_str());
            return NULL;
        }

        const ModuleInfo mi = eim->second;

        params.pushAllowedKeys(mi.params);
        Module *ret = mi.module->alloc_with_comp(comp, params);
        params.popAllowedKeys();
        return ret;
    }
}


SubComponent*
Factory::CreateSubComponent(std::string type, Component* comp, Params& params)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(type);

    // if("sst" == elemlib) {
    //     return CreateCoreModuleWithComponent(elem, comp, params);
    // } else {

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // now look for module
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eis_map_t::iterator eis = found_subcomponents.find(tmp);
    if (eis == found_subcomponents.end()) {
        out.fatal(CALL_INFO, -1,"can't find requested subcomponent %s.\n ", tmp.c_str());
        return NULL;
    }

    const SubComponentInfo si = eis->second;

    params.pushAllowedKeys(si.params);
    SubComponent* ret = si.subcomponent->alloc(comp, params);
    params.popAllowedKeys();
    return ret;
}


void
Factory::RequireEvent(std::string eventname)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(eventname);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    // initializer fires at library load time, so all we have to do is
    // make sure the event actually exists...
    if (found_events.find(eventname) == found_events.end()) {
        out.fatal(CALL_INFO, -1,"can't find event %s in %s\n ", eventname.c_str(),
               searchPaths.c_str() );
    }
}

partitionFunction
Factory::GetPartitioner(std::string name)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(name);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // Look for the partitioner
    std::string tmp = elemlib + "." + elem;
    std::lock_guard<std::recursive_mutex> lock(factoryMutex);
    eip_map_t::iterator eii = found_partitioners.find(tmp);
    if ( eii == found_partitioners.end() ) {
        out.fatal(CALL_INFO, -1,"Error: Unable to find requested partitioner %s, check --help for information on partitioners.\n ", tmp.c_str());
        return NULL;
    }

    const ElementInfoPartitioner *ei = eii->second;
    return ei->func;
}

generateFunction
Factory::GetGenerator(std::string name)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(name);

    // ensure library is already loaded...
    requireLibrary(elemlib);

    // Look for the generator
    std::string tmp = elemlib + "." + elem;

    std::lock_guard<std::recursive_mutex> lock(factoryMutex);
    eig_map_t::iterator eii = found_generators.find(tmp);
    if ( eii == found_generators.end() ) {
        out.fatal(CALL_INFO, -1,"can't find requested partitioner %s.\n ", tmp.c_str());
        return NULL;
    }

    const ElementInfoGenerator *ei = eii->second;
    return ei->func;
}


genPythonModuleFunction
Factory::getPythonModule(std::string name)
{
    std::string elemlib, elem;
    std::tie(elemlib, elem) = parseLoadName(name);

    const ElementLibraryInfo *eli = findLibrary(elemlib, false);
    if ( eli )
        return eli->pythonModuleGenerator;
    return NULL;
}



Params::KeySet_t Factory::create_params_set(const ElementInfoParam *params)
{
    Params::KeySet_t retset;

    if (NULL != params) {
        while (NULL != params->name) {
            retset.insert(params->name);
            params++;
        }
    }

    return retset;
}



bool Factory::hasLibrary(std::string elemlib)
{
    return (NULL != findLibrary(elemlib, false));
}


void Factory::requireLibrary(std::string &elemlib)
{
    (void)findLibrary(elemlib, true);
}


void Factory::getLoadedLibraryNames(std::set<std::string>& lib_names)
{
    for ( eli_map_t::const_iterator i = loaded_libraries.begin();
          i != loaded_libraries.end(); ++i)
        {
            lib_names.insert(i->first);
        }
}

void Factory::loadUnloadedLibraries(const std::set<std::string>& lib_names)
{
    for ( std::set<std::string>::const_iterator i = lib_names.begin();
          i != lib_names.end(); ++i )
        {
            findLibrary(*i);
        }
}
    
const ElementLibraryInfo*
Factory::findLibrary(std::string elemlib, bool showErrors)
{
    const ElementLibraryInfo *eli = NULL;
    std::lock_guard<std::recursive_mutex> lock(factoryMutex);

    eli_map_t::iterator elii = loaded_libraries.find(elemlib);
    if (elii != loaded_libraries.end()) return elii->second;


    eli = loader->loadLibrary(elemlib, showErrors);
    if (NULL == eli) return NULL;

    loaded_libraries[elemlib] = eli;

    if (NULL != eli->components) {
        const ElementInfoComponent *eic = eli->components;
        while (NULL != eic->name) {
            std::string tmp = elemlib + "." + eic->name;
            found_components[tmp] = ComponentInfo(eic, create_params_set(eic->params));
            eic++;
        }
    }

    if (NULL != eli->events) {
        const ElementInfoEvent *eie = eli->events;
        while (NULL != eie->name) {
            std::string tmp = elemlib + "." + eie->name;
            found_events[tmp] = eie;
            if (eie->init != NULL) eie->init();
            eie++;
        }
    }


    if (NULL != eli->modules) {
        const ElementInfoModule *eim = eli->modules;
        while (NULL != eim->name) {
            std::string tmp = elemlib + "." + eim->name;
            found_modules[tmp] = ModuleInfo(eim, create_params_set(eim->params));
            eim++;
        }
    }

    if (NULL != eli->subcomponents ) {
        const ElementInfoSubComponent *eis = eli->subcomponents;
        while (NULL != eis->name) {
            std::string tmp = elemlib + "." + eis->name;
            found_subcomponents[tmp] = SubComponentInfo(eis, create_params_set(eis->params));
            eis++;
        }
    }

    if (NULL != eli->partitioners) {
        const ElementInfoPartitioner *eip = eli->partitioners;
        while (NULL != eip->name) {
            std::string tmp = elemlib + "." + eip->name;
            found_partitioners[tmp] = eip;
            eip++;
        }
    }

    if (NULL != eli->generators) {
        const ElementInfoGenerator *eig = eli->generators;
        while (NULL != eig->name) {
            std::string tmp = elemlib + "." + eig->name;
            found_generators[tmp] = eig;
            eig++;
        }
    }

    return eli;
}



std::pair<std::string, std::string>
Factory::parseLoadName(const std::string& wholename)
{
    std::size_t found = wholename.find_first_of(".");
    if (found == std::string::npos) {
        return make_pair(wholename, wholename);
    } else {
        std::string eli(wholename, 0, found);
        std::string el(wholename, (size_t)(found + 1));
        return make_pair(eli, el);
    }
}

const ElementLibraryInfo* Factory::loadLibrary(std::string name, bool showErrors)
{
    return loader->loadLibrary(name, showErrors);
}

} //namespace SST
