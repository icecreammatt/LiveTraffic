//
//  DataRefs.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LiveTraffic.h"

#include <fstream>
#include <errno.h>
#include <regex>

//
//MARK: external references
//

// provided in LiveTraffic.cpp
void MenuCheckAircraftsDisplayed ( int bChecked );

// provided in LTFlightData.cpp
extern mapLTFlightDataTy mapFd;

// return color into a RGB array as XP likes it
void conv_color (int inCol, float outColor[4])
{
    outColor[0] = float((inCol & 0xFF0000) >> 16) / 255.0f;    // red
    outColor[1] = float((inCol & 0x00FF00) >>  8) / 255.0f;    // green
    outColor[2] = float((inCol & 0x0000FF)      ) / 255.0f;    // blue
    outColor[3] = 1.0f;
}



//
// MARK: Doc8643
//

// global map, which stores the content of the doc8643 file
std::map<std::string, Doc8643> mapDoc8643;
const Doc8643 DOC8643_EMPTY;    // objet returned if Doc8643::get fails

// constructor setting all elements
Doc8643::Doc8643 (std::string&& _manufacturer,
                  std::string&& _model,
                  std::string&& _typeDesignator,
                  std::string&& _classification,
                  std::string&& _wtc) :
manufacturer    (std::move(_manufacturer)),
model           (std::move(_model)),
typeDesignator  (std::move(_typeDesignator)),
classification  (std::move(_classification)),
wtc             (std::move(_wtc))
{}

// return the string for FlightModel matching
Doc8643::operator std::string() const
{
    return wtc + ';' + classification + ';' + typeDesignator + ';' +
    model + ';' + manufacturer;
}

//
// Static functions
//

// reads the Doc8643 file into mapDoc8643
bool Doc8643::ReadDoc8643File ()
{
    // clear the map, just in case
    mapDoc8643.clear();
    
    // Put together path to Doc8643.txt
    std::string path (LTCalcFullPluginPath(PATH_DOC8643_TXT));
    
    // open the file for reading
    std::ifstream fIn (path);
    if (!fIn) {
        // if there is no config file output a warning (we can use defaults)
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                 path.c_str(), sErr);
        return false;
    }
    
    // regular expression to extract individual values, separated by TABs
    enum { DOC_MANU=1, DOC_MODEL, DOC_TYPE, DOC_CLASS, DOC_WTC, DOC_EXPECTED };
    const std::regex re("^([^\\t]+)\\t"                   // manufacturer
                        "([^\\t]+)\\t"                    // model
                        "([[:alnum:]]{2,4})\\t"           // type designator
                        "(-|[AGHLST][C1-8][EJPRT])\\t"    // classification
                        "(-|[HLM]|L/M)");                 // wtc

    // loop over lines of the file
    int errCnt = 0;
    for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
        // read entire line
        char lnBuf[255];
        lnBuf[0] = 0;
        fIn.getline(lnBuf, sizeof(lnBuf));
        std::string text(lnBuf);
        
        // apply the regex to extract values
        std::smatch m;
        std::regex_search(text, m, re);
        
        // add to map (if matched)
        if (m.size() == DOC_EXPECTED) {
            mapDoc8643.emplace(m[DOC_TYPE],
                               Doc8643(m[DOC_MANU],
                                       m[DOC_MODEL],
                                       m[DOC_TYPE],
                                       m[DOC_CLASS],
                                       m[DOC_WTC]));
        } else if (fIn) {
            // I/O was good, but line didn't match
            SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, lnBuf);
            errCnt++;
        } else if (!fIn && !fIn.eof()) {
            // I/O error
			char sErr[SERR_LEN];
			strerror_s(sErr, sizeof(sErr), errno);
			SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, sErr);
            errCnt++;
        }
    }
    
    // close file
    fIn.close();
    
    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 path.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

// return the matching Doc8643 object from the global map
const Doc8643& Doc8643::get (const std::string _type)
try
{
    return mapDoc8643.at(_type);
}
catch (...)
{
    return DOC8643_EMPTY;
}

//MARK: X-Plane Datarefs
const char* DATA_REFS_XP[CNT_DATAREFS_XP] = {
    "sim/time/total_running_time_sec",
    "sim/time/local_time_sec",
    "sim/time/local_date_days",
    "sim/time/use_system_time",
    "sim/time/zulu_time_sec",
};

//
//MARK: DataRefs::dataRefDefinitionT
//

// constant used in dataRefDefinitionT::refCon but denoting to query the address of the respective variable
void* GET_VAR = reinterpret_cast<void*>(INT_MIN);

// list of all datRef definitions offered by LiveTraffic:
DataRefs::dataRefDefinitionT DATA_REFS_LT[] = {
    {"livetraffic/ac/key",                          DataRefs::LTGetAcInfoI, DataRefs::LTSetAcKey,   (void*)DR_AC_KEY, false },
    {"livetraffic/ac/num",                          DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_NUM, false },
    {"livetraffic/ac/on_gnd",                       DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_ON_GND, false },
    {"livetraffic/ac/phase",                        DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_PHASE, false },
    {"livetraffic/ac/lat",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LAT, false },
    {"livetraffic/ac/lon",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LON, false },
    {"livetraffic/ac/alt",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ALT, false },
    {"livetraffic/ac/heading",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEADING, false },
    {"livetraffic/ac/roll",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ROLL, false },
    {"livetraffic/ac/pitch",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_PITCH, false },
    {"livetraffic/ac/speed",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_SPEED, false },
    {"livetraffic/ac/vsi",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_VSI, false },
    {"livetraffic/ac/terrain_alt",                  DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_TERRAIN_ALT, false },
    {"livetraffic/ac/height",                       DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEIGHT, false },
    {"livetraffic/ac/flaps",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_FLAPS, false },
    {"livetraffic/ac/gear",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_GEAR, false },
    {"livetraffic/ac/lights/beacon",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_BEACON, false },
    {"livetraffic/ac/lights/strobe",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_STROBE, false },
    {"livetraffic/ac/lights/nav",                   DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_NAV, false },
    {"livetraffic/ac/lights/landing",               DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_LANDING, false },
    {"livetraffic/ac/bearing",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_BEARING, false },
    {"livetraffic/ac/dist",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_DIST, false },
    {"livetraffic/sim/date",                        DataRefs::LTGetSimDateTime, DataRefs::LTSetSimDateTime, (void*)1, false },
    {"livetraffic/sim/time",                        DataRefs::LTGetSimDateTime, DataRefs::LTSetSimDateTime, (void*)2, false },
    {"livetraffic/cfg/aircrafts_displayed",         DataRefs::LTGetInt, DataRefs::LTSetAircraftsDisplayed, GET_VAR, false },
    {"livetraffic/cfg/auto_start",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/labels",                      DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_col_dyn",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_color",                 DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/log_level",                   DataRefs::LTGetInt, DataRefs::LTSetLogLevel,    GET_VAR, true },
    {"livetraffic/cfg/use_historic_data",           DataRefs::LTGetInt, DataRefs::LTSetUseHistData, GET_VAR, true },
    {"livetraffic/cfg/max_num_ac",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/max_full_num_ac",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/full_distance",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_std_distance",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_refresh_intvl",            DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_buf_period",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ac_outdated_intvl",           DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/adsb_exchange/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/adsb_exchange/historic",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/open_sky/online",         DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/open_sky/ac_masterdata",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/futuredatachn/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/ac_filter",                   DataRefs::LTGetInt, DataRefs::LTSetDebugAcFilter, GET_VAR, true },
    {"livetraffic/dbg/ac_pos",                      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/dbg/log_raw_fd",                  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/model_matching",              DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
};

static_assert(sizeof(DATA_REFS_LT)/sizeof(DATA_REFS_LT[0]) == CNT_DATAREFS_LT,
              "dataRefsLT and DATA_REFS_LT[] differ in number of elements");

// returns the actual address of the variable within DataRefs, which stores the value of interest as per dataRef definition
// (called in case dataRefDefinitionT::refCon == GET_VAR)
void* DataRefs::getVarAddr (dataRefsLT dr)
{
    switch (dr) {
        case DR_CFG_AIRCRAFTS_DISPLAYED:    return &bShowingAircrafts;
        case DR_CFG_AUTO_START:             return &bAutoStart;
        case DR_CFG_LABELS:                 return &labelCfg.i;
        case DR_CFG_LABEL_COL_DYN:          return &bLabelColDynamic;
        case DR_CFG_LABEL_COLOR:            return &labelColor;
        case DR_CFG_LOG_LEVEL:              return &iLogLevel;
        case DR_CFG_USE_HISTORIC_DATA:      return &bUseHistoricData;
        case DR_CFG_MAX_NUM_AC:             return &maxNumAc;
        case DR_CFG_MAX_FULL_NUM_AC:        return &maxFullNumAc;
        case DR_CFG_FULL_DISTANCE:          return &fullDistance;
        case DR_CFG_FD_STD_DISTANCE:        return &fdStdDistance;
        case DR_CFG_FD_REFRESH_INTVL:       return &fdRefreshIntvl;
        case DR_CFG_FD_BUF_PERIOD:          return &fdBufPeriod;
        case DR_CFG_AC_OUTDATED_INTVL:      return &acOutdatedIntvl;

        case DR_DBG_AC_FILTER:              return &uDebugAcFilter;
        case DR_DBG_AC_POS:                 return &bDebugAcPos;
        case DR_DBG_LOG_RAW_FD:             return &bDebugLogRawFd;
        case DR_DBG_MODEL_MATCHING:         return &bDebugModelMatching;
            
        default:
            // flight channels
            if (DR_CHANNEL_FIRST <= dr && dr < DR_CHANNEL_FIRST + CNT_DR_CHANNELS)
                return &bChannel[dr-DR_CHANNEL_FIRST];
            
            // else: must not happen
            LOG_ASSERT(NULL);
            return NULL;
    }
}

//MARK: Inform DataRefEditor about our datarefs
// (see http://www.xsquawkbox.net/xpsdk/mediawiki/DataRefEditor and
//      https://github.com/leecbaker/datareftool/blob/master/src/plugin_custom_dataref.cpp )

// DataRef editors, which we inform about our dataRefs
#define MSG_ADD_DATAREF 0x01000000
const char* DATA_REF_EDITORS[] = {
    "xplanesdk.examples.DataRefEditor",
    "com.leecbaker.datareftool"
};


float LoopCBOneTimeSetup (float, float, int, void*)
{
    static enum ONCE_CB_STATE
    { ONCE_CB_ADD_DREFS=0, ONCE_CB_AUTOSTART, ONCE_CB_DONE }
    eState = ONCE_CB_ADD_DREFS;
    
    switch (eState) {
        case ONCE_CB_ADD_DREFS:
            // loop over all available data ref editor signatures
            for (const char* szDREditor: DATA_REF_EDITORS) {
                // find the plugin by signature
                XPLMPluginID PluginID = XPLMFindPluginBySignature(szDREditor);
                if (PluginID != XPLM_NO_PLUGIN_ID) {
                    // send message regarding each dataRef we offer
                    for ( const DataRefs::dataRefDefinitionT& def: DATA_REFS_LT )
                        XPLMSendMessageToPlugin(PluginID,
                                                MSG_ADD_DATAREF,
                                                (void*)def.getDataName());
                }
            }
            // next: Auto Start, but wait another 2 seconds for that
            eState = ONCE_CB_AUTOSTART;
            return 2;
            
        case ONCE_CB_AUTOSTART:
            // Auto Start display of aircrafts
            if (dataRefs.GetAutoStart())
                dataRefs.SetAircraftsDisplayed(true);
            
            // done
            eState = ONCE_CB_DONE;
            [[fallthrough]];
        default:
            // don't want to be called again
            return 0;
    }
}

// returns offset to UTC in seconds
// https://stackoverflow.com/questions/13804095/get-the-time-zone-gmt-offset-in-c
int timeOffsetUTC()
{
	static int cachedOffset = INT_MIN;

	if (cachedOffset > INT_MIN)
		return cachedOffset;
	else {
		time_t gmt, rawtime = time(NULL);
		struct tm gbuf;
		gmtime_s(&gbuf, &rawtime);

        // Request that mktime() looksup dst in timezone database
		gbuf.tm_isdst = -1;
		gmt = mktime(&gbuf);

		return cachedOffset = (int)difftime(rawtime, gmt);
	}
}

//MARK: Constructor - just plain variable init, no API calls
DataRefs::DataRefs ( logLevelTy initLogLevel ) :
iLogLevel (initLogLevel)
#ifdef DEBUG
,bDebugAcPos (true)
#endif
{
    // override log level in Beta and DEBUG cases
    // (config file is read later, that may reduce the level again)
#ifdef DEBUG
    iLogLevel = logDEBUG;
#else
    if ( LT_BETA_VER_LIMIT )
        iLogLevel = logDEBUG;
#endif
    
    // enable all channels
    for ( int& i: bChannel )
        i = true;

    // Clear the dataRefs arrays
    memset ( adrXP, 0, sizeof(adrXP));
    memset ( adrLT, 0, sizeof(adrLT));
    
    // X-Plane only knows about day in the year, but not the actual year
    // so we assume something:
    //   if LocalDateDays < today's day of year --> current year
    //   if LocalDateDays >= today's day of year --> previous year
    {
        std::tm tm;
        time_t now = time(nullptr);
        localtime_s(&tm, &now);

        dataRefs.iTodaysDayOfYear = tm.tm_yday;
        
        // also compute start of this and last year for sim-time computations
        tm.tm_sec = tm.tm_min = tm.tm_hour = 0;     // 00:00:00h
        tm.tm_mday = 1;                             // 1st of
        tm.tm_mon = 0;                              // January
        tm.tm_isdst = 0;                            // no DST
        tStartThisYear = mktime(&tm);
        
        // now adjust for timezone: current value is midnight as per local time
        // but for our calculations we need midnight UTC
        tStartThisYear += timeOffsetUTC();
        
        // previous year
        tm.tm_year--;
        tStartPrevYear = mktime(&tm);
        tStartPrevYear += timeOffsetUTC();
    }
}

// Find and register dataRefs
bool DataRefs::Init ()
{
    // XP System Path
    char aszPath[512];
    XPLMGetSystemPath ( aszPath );
    XPSystemPath = aszPath;
    
    // Directory Separator provided by XP
    DirSeparator = XPLMGetDirectorySeparator();
    
    // my own plugin path
    pluginID = XPLMGetMyID();
    aszPath[0] = 0;
    XPLMGetPluginInfo(pluginID, NULL, aszPath, NULL, NULL);
    LTPluginPath = aszPath;
    LOG_ASSERT(!LTPluginPath.empty());
    
    // LTPluginPath is now something like "...:Resources:plugins:LiveTraffic:64:mac.xpl"
    // we now reduce the path to the beginning of the plugin:
    // remove the file name
    std::string::size_type pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos);
    // remove the 64 subdirectory, but leave the separator at the end
    pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos+1);
    
    // Fetch all XP-provided data refs and verify if OK
    for ( int i=0; i < CNT_DATAREFS_XP; i++ )
    {
        if ( (adrXP[i] = XPLMFindDataRef (DATA_REFS_XP[i])) == NULL )
        { LOG_MSG(logFATAL,ERR_DATAREF_FIND,DATA_REFS_XP[i]); return false; }
    }

    // register all LiveTraffic-provided dataRefs
    if (!RegisterDataAccessors(DATA_REFS_LT, CNT_DATAREFS_LT))
        return false;

    // Register callback to inform DataRef Editor later on
    XPLMRegisterFlightLoopCallback(LoopCBOneTimeSetup, 1, NULL);
    
    // read Doc8643 file (which we could live without)
    Doc8643::ReadDoc8643File();
    
    // read configuration file if any
    if (!LoadConfigFile())
        return false;
    
    // *** CSL path defaults ***
    // We'll try making this fool-proof but expert-changeable:
    // There are two paths under LiveTraffic that in all normal
    // installations should be supported, especially on initial setup.
    // Experts, however, may want to remove them and keep their CSLs elsewhere
    // or just deactivate the standard directories.
    // So the logic is:
    // 1. Underlying directory _does_ exist and is not empty
    // 2. Entry in vCSLPath does _not_ yet exist
    // then: add an activated entry
    for (std::string stdCSL: { PATH_RESOURCES_CSL, PATH_RESOURCES_SCSL }) {
        // 1. Underlying directory _does_ exist and is not empty
        std::string path (LTCalcFullPluginPath(stdCSL));
        if (LTNumFilesInPath(path) > 0) {
            // 2. Entry in vCSLPath does _not_ yet exist
            CSLPathCfgTy cfg (true, LTRemoveXPSystemPath(path));
            if (std::find(vCSLPaths.cbegin(), vCSLPaths.cend(), cfg) == vCSLPaths.cend()) {
                // insert at beginning
                vCSLPaths.emplace(vCSLPaths.cbegin(), std::move(cfg));
            }
        }
    }
    
    return true;
}

// Unregister (destructor would be too late for reasonable API calls)
void DataRefs::Stop ()
{
    // unregister our dataRefs
    for (XPLMDataRef& dr: adrLT) {
        XPLMUnregisterDataAccessor(dr);
        dr = NULL;
    }
    
    // unregister the callback for informing DRE
    XPLMUnregisterFlightLoopCallback(LoopCBOneTimeSetup, NULL);
}

// call XPLMRegisterDataAccessor
bool DataRefs::RegisterDataAccessors (dataRefDefinitionT aDefs[],
                                      int cnt)
{
    bool bRet = true;
    // loop over all data ref definitions
    for (int i=0; i < cnt; i++)
    {
        dataRefsLT eDataRef = dataRefsLT(i);
        dataRefDefinitionT& def = aDefs[i];
        
        // look up _and update_ refCon first if required
        // (can look up variable addresses only when object is known but not at compile time in definition of DATA_REFS_LT)
        if (def.getRefCon() == GET_VAR)
            def.setRefCon(getVarAddr(eDataRef));
        
        // register data accessor
        if ( (adrLT[i] =
              XPLMRegisterDataAccessor(def.getDataName(),       // inDataName
                                       def.getDataType(),       // inDataType
                                       def.isWriteable(),       // inIsWritable
                                       def.getDatai_f(),        // int
                                       def.setDatai_f(),
                                       def.getDataf_f(),        // float
                                       def.setDataf_f(),
                                       NULL,NULL,               // double
                                       NULL,NULL,               // int array
                                       NULL,NULL,               // float array
                                       NULL,NULL,               // data
                                       def.getRefCon(),         // read refCon
                                       def.getRefCon()          // write refCon
                                       )) == NULL )
        { LOG_MSG(logERR,ERR_DATAREF_ACCESSOR,def.getDataName()); bRet = false; }
    }
    return bRet;
}


//
//MARK: Generic Callbacks
//
// Generic get callbacks: just return the value pointed to...
int     DataRefs::LTGetInt(void* p)     { return *reinterpret_cast<int*>(p); }
float   DataRefs::LTGetFloat(void* p)   { return *reinterpret_cast<float*>(p); }

void    DataRefs::LTSetBool(void* p, int i)
{ *reinterpret_cast<int*>(p) = i != 0; }

//
//MARK: Aircraft Information
//

// finds the a/c ptr based on the key (transpIcao)
bool DataRefs::FetchPAc ()
{
    // if there is no a/c defined yet, BUT there is a debug a/c defined
    // then we use that debug a/c
    if ( keyAc.empty() )
        keyAc = GetDebugAcFilter();
    
    // short-cut if there is still no key
    if ( keyAc.empty() )
        return false;
    
    // find that key's element
    mapLTFlightDataTy::const_iterator fdIter = mapFd.find(keyAc);
    if (fdIter != mapFd.end()) {
        // found, save ptr to a/c
        pAc = fdIter->second.GetAircraft();
        // that pointer might be NULL if a/c has not yet been created!
        return pAc != nullptr;
    }

    // not found, clear all ptr/keys
    pAc = nullptr;
    keyAc.clear();
    return false;
}


// Set a key to define which a/c we return data for
// can be an array index or a transpIcao
void DataRefs::LTSetAcKey(void*, int key)
{
    // sanity check
    if ( key < 0 || key > 0xFFFFFF )
        return;
    
    // default: nothing found
    dataRefs.pAc = nullptr;
    dataRefs.keyAc.clear();
    
    // 0 means reset
    if (key == 0) {
        return;
    }
    // key can be either index or the decimal representation of an transpIcao
    // for any number below number of a/c displayed we assume: index
    else if ( key <= dataRefs.cntAc )
    {
        // let's find the i-th aircraft by looping over all flight data
        // and count those objects, which have an a/c
        int i = 0;
        for (mapLTFlightDataTy::const_iterator fdIter = mapFd.cbegin();
             fdIter != mapFd.cend();
             ++fdIter)
        {
            if (fdIter->second.hasAc()) {       // has an a/c
                if ( ++i == key ) {             // and it's the i-th!
                    dataRefs.keyAc = fdIter->second.key();
                    dataRefs.pAc = fdIter->second.GetAircraft();
                    return;
                }
                
            }
        }
    }
    // so we deal with a transpIcao code
    else
    {
        // the key into mapFd is a 6-digit hex string
        char keyHex[10];
        snprintf ( keyHex, sizeof(keyHex), "%06X", (unsigned int)key );
        
        // now try to find that key and set the pAc ptr
        dataRefs.keyAc = keyHex;
        dataRefs.FetchPAc();
    }
}

// return info 
int DataRefs::LTGetAcInfoI(void* p)
{
    // don't need an a/c pointer for this one:
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_NUM: return dataRefs.cntAc;
    }

    // verify a/c ptr is available
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0;

    // return a/c info
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_KEY: return dataRefs.pAc->fd.keyInt();
        case DR_AC_ON_GND: return dataRefs.pAc->IsOnGrnd();
        case DR_AC_PHASE: return dataRefs.pAc->GetFlightPhase();
        case DR_AC_LIGHTS_BEACON: return dataRefs.pAc->surfaces.lights.bcnLights;
        case DR_AC_LIGHTS_STROBE: return dataRefs.pAc->surfaces.lights.strbLights;
        case DR_AC_LIGHTS_NAV: return dataRefs.pAc->surfaces.lights.navLights;
        case DR_AC_LIGHTS_LANDING: return dataRefs.pAc->surfaces.lights.landLights;
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0;
    }
}

float DataRefs::LTGetAcInfoF(void* p)
{
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0.0;
    
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_LAT:         return (float)dataRefs.pAc->GetPPos().lat();
        case DR_AC_LON:         return (float)dataRefs.pAc->GetPPos().lon();
        case DR_AC_ALT:         return (float)dataRefs.pAc->GetAlt_ft();
        case DR_AC_HEADING:     return (float)dataRefs.pAc->GetHeading();
        case DR_AC_ROLL:        return (float)dataRefs.pAc->GetRoll();
        case DR_AC_PITCH:       return (float)dataRefs.pAc->GetPitch();
        case DR_AC_SPEED:       return (float)dataRefs.pAc->GetSpeed_kt();
        case DR_AC_VSI:         return (float)dataRefs.pAc->GetVSI_ft();
        case DR_AC_TERRAIN_ALT: return (float)dataRefs.pAc->GetTerrainAlt_ft();
        case DR_AC_HEIGHT:      return (float)dataRefs.pAc->GetPHeight_ft();
        case DR_AC_FLAPS:       return (float)dataRefs.pAc->GetFlapsPos();
        case DR_AC_GEAR:        return (float)dataRefs.pAc->GetGearPos();
        case DR_AC_BEARING:     return (float)dataRefs.pAc->GetVecView().angle;
        case DR_AC_DIST:        return (float)dataRefs.pAc->GetVecView().dist;
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0.0;
    }
}

//
//MARK: Config Options
//

// simulated time (seconds since Unix epoch, including fractionals)
double DataRefs::GetSimTime() const
{
    // using historic data means: we take the date configured in X-Plane's date&time settings
    if ( bUseHistoricData )
    {
        // cache parts of the calculation as the difficult part can only
        // change at the full hour
        static double cacheStartOfZuluDay = -1;
        static int lastCalcZHour = -1;
        static double lastLocalDateDays = -1;
        
        // current zulu time of day
        double z  = GetZuluTimeSec();
        // X-Plane's local date, expressed in days since January 1st
        double localDateDays = GetLocalDateDays();

        // if the zulu hour or the date changed since last full calc then the full calc
        // might change, so redo it once and cache the result
        if (int(z/SEC_per_H) != lastCalcZHour ||
            localDateDays != lastLocalDateDays)
        {
            // challenge: Xp doesn't provide "ZuluDateDays". The UTC day might
            // not be the same as the local day we get with GetLocalDateDays.
            // So the approach is as follows: In reality, the time diff between
            // local and zulu can't be more than 12 hours.
            // So if the diff between local and zulu time appears greater than 12 hours
            // we have to adjust the date by one day, which can happen into the past as well as
            // into the future:
            // l = local time
            // z = zulu time
            // 0 = local midnight
            // d = z - l
            //
            // 1 -----0--l---z-----  l < z,   0 <  d <= 12
            // 2 -----0--z---l-----  z < l, -12 <= d <  0
            // 3 --z--0---l--------  z > l,   d > 12,  z-day less    than l-day
            // 4 --l--0---z--------  l > z,   d < -12, z-day greater than l-day
            double l = GetLocalTimeSec();
            double d  = z - l;        // time doesn't move between the two calls within the same drawing frame so the diff is actually a multiple of hours (or at least minutes), but no fractional seconds
            
            // we only need to adapt d if abs(d) is greater than 12 hours
            if ( d > 12 * SEC_per_H )
                localDateDays--;
            else if ( d < -12 * SEC_per_H )
                localDateDays++;
            
            // calculate the right zulu day
            cacheStartOfZuluDay =
                // cater for year-wrap-around as X-Plane doesn't configure the year
                (( localDateDays <= iTodaysDayOfYear ) ? tStartThisYear : tStartPrevYear) +
                // add seconds for each completed day of that year
                localDateDays * SEC_per_D;
            
            // the zulu hour/date we did the calculation for
            lastCalcZHour = int(z / SEC_per_H);
            lastLocalDateDays = localDateDays;
        }

        // add current zulu time to start of zulu day
        return cacheStartOfZuluDay + z;
    }
    else
    {
        // we use current system time (no matter what X-Plane simulates),
        // but lagging behind by the buffering time
        using namespace std::chrono;
        return
            // system time in microseconds
            double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
            // divided by 1000000 to create seconds with fractionals, minus the buffering time
            / 1000000.0 - GetFdBufPeriod();
    }
    
}

// current sim time as human readable string
std::string DataRefs::GetSimTimeString() const
{
    return ts2string(time_t(GetSimTime()));
}

// livetraffic/sim/date and .../time
void DataRefs::LTSetSimDateTime(void* p, int i)
{
    long bDateTime = (long)reinterpret_cast<long long>(p);
    
    // as we are setting a specific date/time we switch XP to "don't use system time"
    dataRefs.SetUseSystemTime(false);
    
    // setting date?
    if ( bDateTime == 1) {
        // range check: if month/day only add _any_ year...doesn't matter (much) for day-of-year calc
        if ( 0101 <= i && i <= 1231 )
            i += 20180000;
        
        // range check: 19000101 <= i <= 29991231
        if ( i < 19000101 || i > 29991231 ) return;
        
        // calculate days since beginning of year: mktime fills that field
        std::tm tm;
        memset (&tm, 0, sizeof(tm));
        tm.tm_year = i / 10000 - 1900;
        tm.tm_mon  = (i % 10000) / 100 - 1;
        tm.tm_mday = i % 100;
        tm.tm_hour = 12;            // pretty safe re local time zone...not absolutely, though
        mktime(&tm);
        
        // set the data ref for local_date_days to adjust X-Planes date immediately
        dataRefs.SetLocalDateDays(tm.tm_yday);
    } else {
        // setting time, range check: 000000 <= i <= 235959
        if ( i < 0 || i > 235959 ) return;
        
        // seconds since midnight
        int sec = i / 10000 * SEC_per_H +           // hour
                  (i % 10000) / 100 * SEC_per_M +   // minute
                  i % 100;
        dataRefs.SetZuluTimeSec((float)sec);
    }
    
    // finally, if we are not already using historic data switch to use it
    //          and force reloading all data
    dataRefs.SetUseHistData(true, true);
}

int DataRefs::LTGetSimDateTime(void* p)
{
    long bDateTime = (long)reinterpret_cast<long long>(p);
    
    // current simulated time, converted to structure
    time_t simTime = (time_t)dataRefs.GetSimTime();
    std::tm tm;
    gmtime_s(&tm, &simTime);

    // asked for date? Return date as human readable number yyyymmdd:
    if ( bDateTime == 1 ) {
        return
            (tm.tm_year + 1900) * 10000 +           // year
            (tm.tm_mon + 1)     *   100 +           // month
            (tm.tm_mday);                           // day of month
    } else {
        // asked for time, return time as human readable number hhmmss:
        return
            tm.tm_hour          * 10000 +           // hour
            tm.tm_min           *   100 +           // minute
            tm.tm_sec;                              // second
    }
}

// Enable/Disable display of aircrafts
void DataRefs::LTSetAircraftsDisplayed(void*, int i)
{ dataRefs.SetAircraftsDisplayed (i); }

void DataRefs::SetAircraftsDisplayed ( int bEnable )
{
    // Do what we are asked to do
    if ( bEnable )
    {
        bShowingAircrafts = LTMainShowAircraft();
    }
    else
    {
        LTMainHideAircraft();
        bShowingAircrafts = 0;
    }
    
    // update menu item's checkmark
    MenuCheckAircraftsDisplayed ( bShowingAircrafts );
}

int DataRefs::ToggleAircraftsDisplayed ()
{
    // set the switch to the other value
    SetAircraftsDisplayed ( !bShowingAircrafts );
    // return the new status
    return bShowingAircrafts;
}

// Log Level
void DataRefs::LTSetLogLevel(void*, int i)
{ dataRefs.SetLogLevel(i); }

void DataRefs::SetLogLevel ( int i )
{
    if ( i <= logDEBUG )       iLogLevel = logDEBUG;
    else if ( i >= logFATAL )  iLogLevel = logFATAL ;
    else                       iLogLevel = logLevelTy(i);
}

// switch usage of historic data
void DataRefs::LTSetUseHistData(void*, int useHistData)
{
    dataRefs.SetUseHistData(useHistData != 0, false);
}

// switch usage of historic data, return success
bool DataRefs::SetUseHistData (bool bUseHistData, bool bForceReload)
{
    // short-cut if no actual change...cause changing it is expensive
    if ( !bForceReload && dataRefs.bUseHistoricData == (int)bUseHistData )
        return true;
    
    // change to historical data but running with system time?
    if ( bUseHistData && dataRefs.GetUseSystemTime() )
    {
        SHOW_MSG(logERR, MSG_HIST_WITH_SYS_TIME);
        return false;
    }
    
    // if we change this setting while running
    // we 'simulate' a re-initialization
    if (pluginState >= STATE_ENABLED) {
        // remove all existing aircrafts
        bool bShowAc = dataRefs.GetAircraftsDisplayed();
        dataRefs.SetAircraftsDisplayed(false);

        // disable myself / stop all connections
        LTMainDisable();
        
        // Now set the new setting
        dataRefs.bUseHistoricData = bUseHistData;
        
        // create the connections to flight data
        if ( LTMainEnable() ) {
            // display aircraft (if that was the case previously)
            dataRefs.SetAircraftsDisplayed(bShowAc);
            return true;
        }
        else {
            return false;
        }
    }
    else {
        // not yet running, i.e. init phase: just set the value
        dataRefs.bUseHistoricData = bUseHistData;
        return true;
    }
}

// set a config value and validate (all of) them
void DataRefs::LTSetCfgValue (void* p, int val)
{ dataRefs.SetCfgValue(p, val); }

bool DataRefs::SetCfgValue (void* p, int val)
{
    // we don't exactly know which parameter p points to...
    // ...we just set it, validate all of them, and reset in case validation fails
    int oldVal = *reinterpret_cast<int*>(p);
    *reinterpret_cast<int*>(p) = val;
    
    // any configuration value invalid?
    if (labelColor      < 0                 || labelColor       > 0xFFFFFF ||
        maxNumAc        < 5                 || maxNumAc         > 100   ||
        maxFullNumAc    < 5                 || maxFullNumAc     > 100   ||
        fullDistance    < 1                 || fullDistance     > 100   ||
        fdStdDistance   < 5                 || fdStdDistance    > 100   ||
        fdRefreshIntvl  < 10                || fdRefreshIntvl   > 5*60  ||
        fdBufPeriod     < fdRefreshIntvl    || fdBufPeriod      > 5*60  ||
        acOutdatedIntvl < 2*fdRefreshIntvl  || acOutdatedIntvl  > 5*60)
    {
        // undo change
        *reinterpret_cast<int*>(p) = oldVal;
        return false;
    }
    
    // Tell XPMP if we need labels
    if (labelCfg.i > 0)
        XPMPEnableAircraftLabels();
    else
        XPMPDisableAircraftLabels();
    
    // success
    return true;
}

// return color into a RGB array as XP likes it
void DataRefs::GetLabelColor (float outColor[4]) const
{
    conv_color(labelColor, outColor);
}

//
//MARK: Debug Options
//

// return current a/c filter
std::string DataRefs::GetDebugAcFilter() const
{
    char key[7];
    if ( !uDebugAcFilter ) return std::string();
    
    // convert to hex representation
    snprintf(key,sizeof(key),"%06X",uDebugAcFilter);
    return key;
}

// sets A/C filter
void DataRefs::LTSetDebugAcFilter( void* /*inRefcon*/, int i )
{
    bool bWasFilterDefined = dataRefs.uDebugAcFilter != 0;
    
    // match hex range of transpIcao codes
    if ( 0x000000 <= i && i <= 0xFFFFFF ) {
        dataRefs.uDebugAcFilter = unsigned(i);
        
        // also set the key for the a/c info datarefs
        if (i > 0x000000) {
            LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)), i);
            LOG_MSG(logWARN,DBG_FILTER_AC,
                    dataRefs.GetDebugAcFilter().c_str());
        } else if (bWasFilterDefined) {
            LOG_MSG(logWARN,DBG_FILTER_AC_REMOVED);
        }
    }
}

//
// MARK: DataRefs::dataRefDefinitionT
//

// get the actual current value (by calling the getData?_f function)
int DataRefs::dataRefDefinitionT::getDatai () const
{
    if (dataType != xplmType_Int || ifRead == NULL)
        return 0;

    LOG_ASSERT(refCon != GET_VAR);
    return (ifRead)(refCon);
}

float DataRefs::dataRefDefinitionT::getDataf () const
{
    if (dataType != xplmType_Float || ffRead == NULL)
        return NAN;

    LOG_ASSERT(refCon != GET_VAR);
    return (ffRead)(refCon);
}


// compiles string for storage in config file
std::string DataRefs::dataRefDefinitionT::GetConfigString() const
{
    // short-cut: not for saving in config file
    if (!bCfgFile)
        return std::string();
    
    // put together dataRef's dataName and
    // add the current value, which might be int or float
    switch (dataType) {
        case xplmType_Int:
            return dataName + ' ' + std::to_string(getDatai());
        case xplmType_Float:
            return dataName + ' ' + std::to_string(getDataf());
        default:
            // else: must not happen
            LOG_ASSERT(NULL);
            return std::string();
    }
}


// write values to the dataRef
void DataRefs::dataRefDefinitionT::setData (int i)
{
    if (dataType == xplmType_Int && ifWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ifWrite) (refCon, i);
    }
}

void DataRefs::dataRefDefinitionT::setData (float f)
{
    if (dataType == xplmType_Float && ffWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ffWrite) (refCon, f);
    }
}

// assumes the string is a number, which will be converted to the
// appropriate type and then passed on to the other setData functions
void DataRefs::dataRefDefinitionT::setData (const std::string& s)
{
    try {
        switch (dataType) {
            case xplmType_Int:
                setData (std::stoi(s));
                break;
            case xplmType_Float:
                setData (std::stof(s));
                break;
            default:
                // else: must not happen
                LOG_ASSERT(NULL);
        }
    }
    catch (const std::invalid_argument& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
    catch (const std::out_of_range& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
}


//MARK: Config File

bool DataRefs::LoadXPlanePrf()
{
    int hdr = renopt_HDR_antial > 0 ? 1: 0;
    int hdr_antial = renopt_HDR_antial;
    
    // open a config file
    std::string sFileName (LTCalcFullPath(PATH_XPLANE_PRF));
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no config file just return...that's no problem, we use defaults
        if (errno == ENOENT)
            return true;
        
        // something else happened
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                 sFileName.c_str(), sErr);
        return false;
    }

    // read line-by-line
    char lnBuf[255];
    while (fIn) {
        // read line and break into tokens, delimited by spaces
        lnBuf[0] = 0;
        fIn.getline(lnBuf, sizeof(lnBuf));
        const std::vector<std::string> ln = str_tokenize(lnBuf, " ");
        
        // we expect 2 tokens
        if (ln.size() != 2)
            continue;
        
        // read values from tokens if found
		if (ln[0] == XPPRF_RENOPT_HDR)                  // XP10
			hdr = stoi(ln[1]);
		else if (ln[0] == XPPRF_EFFECTS_04)             // XP11
			hdr = stoi(ln[1]) >= 3 ? 1 : 0;
        else if (ln[0] == XPPRF_RENOPT_HDR_ANTIAL)
            hdr_antial = stoi(ln[1]);
    }
    
    // close file
    fIn.close();
    
    // save value, only valid if HDR is enabled
    renopt_HDR_antial = hdr ? hdr_antial : 0;
    return true;
}

bool DataRefs::LoadConfigFile()
{
    // open a config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no config file just return...that's no problem, we use defaults
        if (errno == ENOENT)
            return true;
        
        // something else happened
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // first line is supposed to be the version - and we know of exactly one:
    // read entire line
    std::vector<std::string> ln;
    char lnBuf[255];
    lnBuf[0] = 0;
    fIn.getline(lnBuf, sizeof(lnBuf));
    ln = str_tokenize(lnBuf, " ");

    if (!fIn || ln.size() != 2 ||
        ln[0] != LIVE_TRAFFIC ||
        ln[1] != LT_CFG_VERSION)
    {
        SHOW_MSG(logERR, ERR_CFG_FILE_VER, sFileName.c_str());
        return false;
    }
    
    // *** DataRefs ***
    // then follow the config entries
    // supposingly they are just 'dataRef <space> value',
    // but to prevent misuse we certainly validate that we support
    // those dataRefs.
    // The file could end with a [CSLPaths] section, too.
    int errCnt = 0;
    while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN) {
        // read line and break into tokens, delimited by spaces
        lnBuf[0] = 0;
        fIn.getline(lnBuf, sizeof(lnBuf));
        ln = str_tokenize(lnBuf, " ");
        
        // break out of loop if reading [CSLPaths]
        if (ln.size() == 1 && ln[0] == CFG_CSL_SECTION)
            break;

        // otherwise should be 2 tokens
        if (ln.size() != 2) {
            // wrong number of words in that line
            LOG_MSG(logWARN,ERR_CFG_FILE_WORDS, sFileName.c_str(), lnBuf);
            errCnt++;
            continue;
        }
        
        // did read a name and a value?
        const std::string& sDataRef = ln[0];
        const std::string& sVal     = ln[1];
        if (!sDataRef.empty() && !sVal.empty()) {
            // verify that we know that name
            dataRefDefinitionT* i = std::find_if(std::begin(DATA_REFS_LT),
                                                 std::end(DATA_REFS_LT),
                                                 [&](const DataRefs::dataRefDefinitionT& def)
                                                 { return def.getDataNameStr() == sDataRef; } );
            if ( i != nullptr && i != std::cend(DATA_REFS_LT) &&
                 i->isCfgFile()) {         // and it is a configurable one
                // *** valid config entry, now process it ***
                i->setData(sVal);
            }
            // *** Strings ***
            else if (sDataRef == CFG_DEFAULT_AC_TYPE)
                dataRefs.SetDefaultAcIcaoType(sVal);
            else if (sDataRef == CFG_DEFAULT_CAR_TYPE)
                dataRefs.SetDefaultCarIcaoType(sVal);
            else
            {
                // unknown config entry, ignore
                LOG_MSG(logWARN,ERR_CFG_FILE_IGNORE,
                        sDataRef.c_str(), sFileName.c_str());
                errCnt++;
            }
        }
        
    }
    
    // *** [CSLPaths] ***
    // maybe there's a [CSLPath] section?
    if (fIn && errCnt <= ERR_CFG_FILE_MAXWARN &&
        ln.size() == 1 && ln[0] == CFG_CSL_SECTION)
    {
        
        // loop until EOF
        while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN)
        {
            // read line and break into tokens, delimited by spaces
            lnBuf[0] = 0;
            fIn.getline(lnBuf, sizeof(lnBuf));

            // skip empty lines without warning
            if (lnBuf[0] == 0)
                continue;
            
            // line has to start with 0 or 1 and | to separate "enabled?" from path
            ln = str_tokenize(lnBuf, "|");
            if (ln.size() != 2 ||
                ln[0].size() != 1 ||
                (ln[0][0] != '0' && ln[0][0] != '1'))
            {
                LOG_MSG(logWARN, ERR_CFG_CSL_INVALID, sFileName.c_str(), lnBuf);
                errCnt++;
                continue;
            }
            
            // enabled?
            bool bEnabled = ln[0][0] == '1';
            
            // add the path to the list (unvalidated!)
            vCSLPaths.emplace_back(bEnabled, std::move(ln[1]));
        }
    }

    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // close file
    fIn.close();

    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 sFileName.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

bool DataRefs::SaveConfigFile()
{
    // open an output config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ofstream fOut (sFileName, std::ios_base::out | std::ios_base::trunc);
    if (!fOut) {
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_OUT,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // save application and version first...maybe we need to know it in
    // future versions for conversion efforts - who knows?
    fOut << LIVE_TRAFFIC << ' ' << LT_CFG_VERSION << '\n';
    
    // *** DataRefs ***
    // loop over our LiveTraffic values and store those meant to be stored
    for (const DataRefs::dataRefDefinitionT& def: DATA_REFS_LT)
        if (def.isCfgFile())                   // only for values which are to be saved
            fOut << def.GetConfigString() << '\n';
    
    // *** Strings ***
    fOut << CFG_DEFAULT_AC_TYPE << ' ' << dataRefs.GetDefaultAcIcaoType() << '\n';
    fOut << CFG_DEFAULT_CAR_TYPE << ' ' << dataRefs.GetDefaultCarIcaoType() << '\n';

    // *** [CSLPatchs] ***
    // add section of CSL paths to the end
    if (!vCSLPaths.empty()) {
        fOut << CFG_CSL_SECTION << '\n';
        for (const DataRefs::CSLPathCfgTy& cslPath: vCSLPaths)
            if (!cslPath.empty())
                fOut << (cslPath.enabled() ? "1|" : "0|") <<
                LTRemoveXPSystemPath(cslPath.path) << '\n';
    }
    
    // some error checking towards the end
    if (!fOut) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_WRITE,
                 sFileName.c_str(), sErr);
        fOut.close();
        return false;
    }

    // flush (which we explicitely did not do for each line for performance reasons) and close
    fOut.flush();
    fOut.close();
        
    return true;
}

// Save a new/changed CSL path
void DataRefs::SaveCSLPath(int idx, const CSLPathCfgTy path)
{
    // make sure the array of paths is large enough
    while (idx >= vCSLPaths.size())
        vCSLPaths.push_back({});
    
    // then store the actual path
    vCSLPaths[idx] = path;
}

// Load a CSL package interactively
bool DataRefs::LoadCSLPackage(int idx)
{
    const std::string pathRelated (LTCalcFullPluginPath(PATH_RELATED_TXT));
    const std::string pathDoc8643 (LTCalcFullPluginPath(PATH_DOC8643_TXT));
    
    if (idx < vCSLPaths.size()) {
        // enabled, path could be relative to X-Plane
        const std::string path = LTCalcFullPath(vCSLPaths[idx].path);
        
        // doesn't exist? has no files?
        if (LTNumFilesInPath(path) < 1) {
            SHOW_MSG(logERR, ERR_CFG_CSL_EMPTY, path.c_str());
        }
        else
        {
            // try loading the package
            const char* cszResult = XPMPLoadCSLPackage(path.c_str(),
                                                       pathRelated.c_str(),
                                                       pathDoc8643.c_str());
            
            // Addition of CSL package failed?
            if ( cszResult[0] ) {
                SHOW_MSG(logERR,ERR_XPMP_ADD_CSL, cszResult);
            } else {
                SHOW_MSG(logMSG,MSG_CSL_PACKAGE_LOADED, vCSLPaths[idx].path.c_str());
                return true;
            }
        }
    }
    
    // didn't work for some reason
    return false;
}


// sets the default a/c icao type after validation with Doc8643
bool DataRefs::SetDefaultAcIcaoType(const std::string type)
{
    if (Doc8643::get(type) != DOC8643_EMPTY) {
        sDefaultAcIcaoType = type;
        LOG_MSG(logINFO,CFG_DEFAULT_AC_TYP_INFO,sDefaultAcIcaoType.c_str());
        return true;
    }

    // invalid type
    SHOW_MSG(logWARN,ERR_CFG_AC_DEFAULT,type.c_str(),
             sDefaultAcIcaoType.c_str());
    return false;
}

// sets default car type. this is a fake value, so no validation agains Doc8643
// but still needs to be 1 through 4 characters long
bool DataRefs::SetDefaultCarIcaoType(const std::string type)
{
    if (1 <= type.length() && type.length() <= 4) {
        sDefaultCarIcaoType = type;
        LOG_MSG(logINFO,CFG_DEFAULT_CAR_TYP_INFO,sDefaultCarIcaoType.c_str());
        return true;
    }
    
    // invalid
    LOG_MSG(logWARN,ERR_CFG_CAR_DEFAULT,type.c_str(),
            sDefaultCarIcaoType.c_str());
    return false;
}

//MARK: Processed values (static functions)

// return the camera's position in world coordinates
positionTy DataRefs::GetViewPos()
{
    XPLMCameraPosition_t camPos = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // get the dataref values for current view pos, which are in local coordinates
    XPLMReadCameraPosition(&camPos);
    // convert to world coordinates and return them
    double lat, lon, alt;
    XPLMLocalToWorld(camPos.x, camPos.y, camPos.z,
                     &lat, &lon, &alt);
    return positionTy(lat,lon,alt);
}

// return the direction the camera is looking to
double DataRefs::GetViewHeading()
{
    XPLMCameraPosition_t camPos = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // get the dataref values for current view pos, which are in local coordinates
    XPLMReadCameraPosition(&camPos);
    return camPos.heading;
}
