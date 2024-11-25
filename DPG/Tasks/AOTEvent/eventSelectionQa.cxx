// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "map"

#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Common/DataModel/EventSelection.h"
#include "Common/CCDB/EventSelectionParams.h"
#include "CCDB/BasicCCDBManager.h"
#include "Framework/HistogramRegistry.h"
#include "CommonDataFormat/BunchFilling.h"
#include "DataFormatsParameters/GRPLHCIFData.h"
#include "DataFormatsParameters/GRPECSObject.h"
#include "TH1F.h"
#include "TH2F.h"

using namespace o2::framework;
using namespace o2;
using namespace o2::aod::evsel;

using BCsRun2 = soa::Join<aod::BCs, aod::Run2BCInfos, aod::Timestamps, aod::BcSels, aod::Run2MatchedToBCSparse>;
using BCsRun3 = soa::Join<aod::BCs, aod::Timestamps, aod::BcSels, aod::Run3MatchedToBCSparse>;
using ColEvSels = soa::Join<aod::Collisions, aod::EvSels>;
using FullTracksIU = soa::Join<aod::TracksIU, aod::TracksExtra>;

struct EventSelectionQaTask {
  Configurable<bool> isMC{"isMC", 0, "0 - data, 1 - MC"};
  Configurable<int> nGlobalBCs{"nGlobalBCs", 100000, "number of global bcs"};
  Configurable<double> minOrbitConf{"minOrbit", 0, "minimum orbit"};
  Configurable<int> nOrbitsConf{"nOrbits", 10000, "number of orbits"};
  Configurable<bool> isLowFlux{"isLowFlux", 1, "1 - low flux (pp, pPb), 0 - high flux (PbPb)"};

  uint64_t minGlobalBC = 0;
  Service<o2::ccdb::BasicCCDBManager> ccdb;
  HistogramRegistry histos{"Histos", {}, OutputObjHandlingPolicy::AnalysisObject};
  bool* applySelection = NULL;
  int nBCsPerOrbit = 3564;
  int lastRunNumber = -1;
  int nOrbits = nOrbitsConf;
  double minOrbit = minOrbitConf;
  int64_t bcSOR = 0;                      // global bc of the start of the first orbit, setting 0 by default for unanchored MC
  int64_t nBCsPerTF = 128 * nBCsPerOrbit; // duration of TF in bcs, should be 128*3564 or 32*3564, setting 128 orbits by default sfor unanchored MC
  std::bitset<o2::constants::lhc::LHCMaxBunches> beamPatternA;
  std::bitset<o2::constants::lhc::LHCMaxBunches> beamPatternC;
  std::bitset<o2::constants::lhc::LHCMaxBunches> bcPatternA;
  std::bitset<o2::constants::lhc::LHCMaxBunches> bcPatternC;
  std::bitset<o2::constants::lhc::LHCMaxBunches> bcPatternB;

  SliceCache cache;
  Partition<aod::Tracks> tracklets = (aod::track::trackType == static_cast<uint8_t>(o2::aod::track::TrackTypeEnum::Run2Tracklet));

  int32_t findClosest(int64_t globalBC, std::map<int64_t, int32_t>& bcs)
  {
    auto it = bcs.lower_bound(globalBC);
    int64_t bc1 = it->first;
    int32_t index1 = it->second;
    if (it != bcs.begin())
      --it;
    int64_t bc2 = it->first;
    int32_t index2 = it->second;
    int64_t dbc1 = std::abs(bc1 - globalBC);
    int64_t dbc2 = std::abs(bc2 - globalBC);
    return (dbc1 <= dbc2) ? index1 : index2;
  }

  void init(InitContext&)
  {
    minGlobalBC = uint64_t(minOrbit) * nBCsPerOrbit;

    // ccdb->setURL("http://ccdb-test.cern.ch:8080");
    ccdb->setURL("http://alice-ccdb.cern.ch");
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();

    const AxisSpec axisMultV0M{1000, 0., isLowFlux ? 40000. : 40000., "V0M multiplicity"};
    const AxisSpec axisMultV0A{1000, 0., isLowFlux ? 40000. : 200000., "V0A multiplicity"};
    const AxisSpec axisMultV0C{1000, 0., isLowFlux ? 30000. : 30000., "V0C multiplicity"};
    const AxisSpec axisMultT0A{1000, 0., isLowFlux ? 10000. : 200000., "T0A multiplicity"};
    const AxisSpec axisMultT0C{1000, 0., isLowFlux ? 2000. : 70000., "T0C multiplicity"};
    const AxisSpec axisMultT0M{1000, 0., isLowFlux ? 12000. : 270000., "T0M multiplicity"};
    const AxisSpec axisMultFDA{1000, 0., isLowFlux ? 50000. : 40000., "FDA multiplicity"};
    const AxisSpec axisMultFDC{1000, 0., isLowFlux ? 50000. : 40000., "FDC multiplicity"};
    const AxisSpec axisMultZNA{1000, 0., isLowFlux ? 1000. : 400., "ZNA multiplicity"};
    const AxisSpec axisMultZNC{1000, 0., isLowFlux ? 1000. : 400., "ZNC multiplicity"};
    const AxisSpec axisNtracklets{200, 0., isLowFlux ? 200. : 6000., "n tracklets"};
    const AxisSpec axisNclusters{200, 0., isLowFlux ? 1000. : 20000., "n clusters"};
    const AxisSpec axisMultOnlineV0M{400, 0., isLowFlux ? 8000. : 40000., "Online V0M"};
    const AxisSpec axisMultOnlineFOR{300, 0., isLowFlux ? 300. : 1200., "Online FOR"};
    const AxisSpec axisMultOflineFOR{300, 0., isLowFlux ? 300. : 1200., "Ofline FOR"};

    const AxisSpec axisTime{700, -35., 35., ""};
    const AxisSpec axisTimeDif{100, -10., 10., ""};
    const AxisSpec axisTimeSum{100, -10., 10., ""};
    const AxisSpec axisGlobalBCs{nGlobalBCs, 0., static_cast<double>(nGlobalBCs), ""};
    const AxisSpec axisBCs{nBCsPerOrbit, 0., static_cast<double>(nBCsPerOrbit), ""};
    const AxisSpec axisNcontrib{200, 0., isLowFlux ? 200. : 8000., "n contributors"};
    const AxisSpec axisEta{100, -1., 1., "track #eta"};
    const AxisSpec axisColTimeRes{1500, 0., 1500., "collision time resolution (ns)"};
    const AxisSpec axisBcDif{600, -300., 300., "collision bc difference"};
    const AxisSpec axisAliases{kNaliases, 0., static_cast<double>(kNaliases), ""};
    const AxisSpec axisSelections{kNsel, 0., static_cast<double>(kNsel), ""};
    const AxisSpec axisVtxZ{500, -25., 25., ""};
    const AxisSpec axisVtxXY{500, -1., 1., ""};

    histos.add("hTimeV0Aall", "All bcs;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Call", "All bcs;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAall", "All bcs;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCall", "All bcs;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Aall", "All bcs;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Call", "All bcs;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAall", "All bcs;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCall", "All bcs;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZACall", "All bcs; ZNC-ZNA time (ns); ZNC+ZNA time (ns)", kTH2F, {axisTimeDif, axisTimeSum});
    histos.add("hTimeV0Abga", "BeamA-only bcs;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Cbga", "BeamA-only bcs;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAbga", "BeamA-only bcs;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCbga", "BeamA-only bcs;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Abga", "BeamA-only bcs;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Cbga", "BeamA-only bcs;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAbga", "BeamA-only bcs;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCbga", "BeamA-only bcs;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Abgc", "BeamC-only bcs;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Cbgc", "BeamC-only bcs;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAbgc", "BeamC-only bcs;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCbgc", "BeamC-only bcs;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Abgc", "BeamC-only bcs;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Cbgc", "BeamC-only bcs;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAbgc", "BeamC-only bcs;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCbgc", "BeamC-only bcs;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Aref", "Reference bcs;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Cref", "Reference bcs;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAref", "Reference bcs;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCref", "Reference bcs;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Aref", "Reference bcs;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Cref", "Reference bcs;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAref", "Reference bcs;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCref", "Reference bcs;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZACref", "Reference bcs; ZNC-ZNA time (ns); ZNC+ZNA time (ns)", kTH2F, {axisTimeDif, axisTimeSum});
    histos.add("hTimeV0Acol", "All events;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Ccol", "All events;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAcol", "All events;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCcol", "All events;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Acol", "All events;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Ccol", "All events;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAcol", "All events;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCcol", "All events;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZACcol", "All events; ZNC-ZNA time (ns); ZNC+ZNA time (ns)", kTH2F, {axisTimeDif, axisTimeSum});
    histos.add("hTimeV0Aacc", "Accepted events;V0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeV0Cacc", "Accepted events;V0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNAacc", "Accepted events;ZNA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZNCacc", "Accepted events;ZNC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Aacc", "Accepted events;T0A time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeT0Cacc", "Accepted events;T0C time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDAacc", "Accepted events;FDA time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeFDCacc", "Accepted events;FDC time (ns);Entries", kTH1F, {axisTime});
    histos.add("hTimeZACacc", "Accepted events; ZNC-ZNA time (ns); ZNC+ZNA time (ns)", kTH2F, {axisTimeDif, axisTimeSum});
    histos.add("hSPDClsVsTklCol", "All events", kTH2F, {axisNtracklets, axisNclusters});
    histos.add("hV0C012vsTklCol", "All events;n tracklets;V0C012 multiplicity", kTH2F, {axisNtracklets, axisMultV0C});
    histos.add("hV0MOnVsOfCol", "All events", kTH2F, {axisMultV0M, axisMultOnlineV0M});
    histos.add("hSPDOnVsOfCol", "All events", kTH2F, {axisMultOflineFOR, axisMultOnlineFOR});
    histos.add("hV0C3vs012Col", "All events;V0C012 multiplicity;V0C3 multiplicity", kTH2F, {axisMultV0C, axisMultV0C});
    histos.add("hSPDClsVsTklAcc", "Accepted events", kTH2F, {axisNtracklets, axisNclusters});
    histos.add("hV0C012vsTklAcc", "Accepted events;n tracklets;V0C012 multiplicity", kTH2F, {axisNtracklets, axisMultV0C});
    histos.add("hV0MOnVsOfAcc", "Accepted events", kTH2F, {axisMultV0M, axisMultOnlineV0M});
    histos.add("hSPDOnVsOfAcc", "Accepted events", kTH2F, {axisMultOflineFOR, axisMultOnlineFOR});
    histos.add("hV0C3vs012Acc", "Accepted events;V0C012 multiplicity;V0C3 multiplicity", kTH2F, {axisMultV0C, axisMultV0C});

    histos.add("hColCounterAll", "", kTH1F, {axisAliases});
    histos.add("hColCounterAcc", "", kTH1F, {axisAliases});
    histos.add("hBcCounterAll", "", kTH1F, {axisAliases});
    histos.add("hSelCounter", "", kTH1F, {axisSelections});
    histos.add("hSelMask", "", kTH1F, {axisSelections});

    histos.add("hGlobalBcAll", "", kTH1F, {axisGlobalBCs});
    histos.add("hGlobalBcCol", "", kTH1F, {axisGlobalBCs});
    histos.add("hGlobalBcFT0", "", kTH1F, {axisGlobalBCs});
    histos.add("hGlobalBcFV0", "", kTH1F, {axisGlobalBCs});
    histos.add("hGlobalBcFDD", "", kTH1F, {axisGlobalBCs});
    histos.add("hGlobalBcZDC", "", kTH1F, {axisGlobalBCs});

    histos.add("hBcA", "", kTH1F, {axisBCs});
    histos.add("hBcC", "", kTH1F, {axisBCs});
    histos.add("hBcB", "", kTH1F, {axisBCs});
    histos.add("hBcAll", "", kTH1F, {axisBCs});
    histos.add("hBcCol", "", kTH1F, {axisBCs});
    histos.add("hBcTVX", "", kTH1F, {axisBCs});
    histos.add("hBcFT0", "", kTH1F, {axisBCs});
    histos.add("hBcFV0", "", kTH1F, {axisBCs});
    histos.add("hBcFDD", "", kTH1F, {axisBCs});
    histos.add("hBcZDC", "", kTH1F, {axisBCs});
    histos.add("hBcColTOF", "", kTH1F, {axisBCs});
    histos.add("hBcColTRD", "", kTH1F, {axisBCs});
    histos.add("hBcTrackTOF", "", kTH1F, {axisBCs});
    histos.add("hBcTrackTRD", "", kTH1F, {axisBCs});

    histos.add("hMultV0Aall", "All bcs", kTH1F, {axisMultV0A});
    histos.add("hMultV0Call", "All bcs", kTH1F, {axisMultV0C});
    histos.add("hMultZNAall", "All bcs", kTH1F, {axisMultZNA});
    histos.add("hMultZNCall", "All bcs", kTH1F, {axisMultZNC});
    histos.add("hMultT0Aall", "All bcs", kTH1F, {axisMultT0A});
    histos.add("hMultT0Call", "All bcs", kTH1F, {axisMultT0C});
    histos.add("hMultFDAall", "All bcs", kTH1F, {axisMultFDA});
    histos.add("hMultFDCall", "All bcs", kTH1F, {axisMultFDC});
    histos.add("hMultV0Aref", "Reference bcs", kTH1F, {axisMultV0A});
    histos.add("hMultV0Cref", "Reference bcs", kTH1F, {axisMultV0C});
    histos.add("hMultZNAref", "Reference bcs", kTH1F, {axisMultZNA});
    histos.add("hMultZNCref", "Reference bcs", kTH1F, {axisMultZNC});
    histos.add("hMultT0Aref", "Reference bcs", kTH1F, {axisMultT0A});
    histos.add("hMultT0Cref", "Reference bcs", kTH1F, {axisMultT0C});
    histos.add("hMultFDAref", "Reference bcs", kTH1F, {axisMultFDA});
    histos.add("hMultFDCref", "Reference bcs", kTH1F, {axisMultFDC});
    histos.add("hMultV0Mcol", "All events", kTH1F, {axisMultV0M});
    histos.add("hMultV0Acol", "All events", kTH1F, {axisMultV0A});
    histos.add("hMultV0Ccol", "All events", kTH1F, {axisMultV0C});
    histos.add("hMultZNAcol", "All events", kTH1F, {axisMultZNA});
    histos.add("hMultZNCcol", "All events", kTH1F, {axisMultZNC});
    histos.add("hMultT0Acol", "All events", kTH1F, {axisMultT0A});
    histos.add("hMultT0Ccol", "All events", kTH1F, {axisMultT0C});
    histos.add("hMultFDAcol", "All events", kTH1F, {axisMultFDA});
    histos.add("hMultFDCcol", "All events", kTH1F, {axisMultFDC});
    histos.add("hMultV0Macc", "Accepted events", kTH1F, {axisMultV0M});
    histos.add("hMultV0Aacc", "Accepted events", kTH1F, {axisMultV0A});
    histos.add("hMultV0Cacc", "Accepted events", kTH1F, {axisMultV0C});
    histos.add("hMultZNAacc", "Accepted events", kTH1F, {axisMultZNA});
    histos.add("hMultZNCacc", "Accepted events", kTH1F, {axisMultZNC});
    histos.add("hMultT0Aacc", "Accepted events", kTH1F, {axisMultT0A});
    histos.add("hMultT0Cacc", "Accepted events", kTH1F, {axisMultT0C});
    histos.add("hMultFDAacc", "Accepted events", kTH1F, {axisMultFDA});
    histos.add("hMultFDCacc", "Accepted events", kTH1F, {axisMultFDC});

    histos.add("hMultT0Abga", "A-side beam-gas events", kTH1F, {axisMultT0A});
    histos.add("hMultT0Abgc", "C-side beam-gas events", kTH1F, {axisMultT0A});
    histos.add("hMultT0Cbga", "A-side beam-gas events", kTH1F, {axisMultT0C});
    histos.add("hMultT0Cbgc", "C-side beam-gas events", kTH1F, {axisMultT0C});

    histos.add("hMultT0Mall", "BCs with collisions", kTH1F, {axisMultT0M});
    histos.add("hMultT0Mref", "", kTH1F, {axisMultT0M});
    histos.add("hMultT0Mtvx", "", kTH1F, {axisMultT0M});
    histos.add("hMultT0Mzac", "", kTH1F, {axisMultT0M});
    histos.add("hMultT0Mpup", "BCs with pileup", kTH1F, {axisMultT0M});

    histos.add("hMultT0Atvx", "", kTH1F, {axisMultT0A});
    histos.add("hMultT0Ctvx", "", kTH1F, {axisMultT0C});
    histos.add("hMultT0Azac", "", kTH1F, {axisMultT0A});
    histos.add("hMultT0Czac", "", kTH1F, {axisMultT0C});

    histos.add("hColTimeResVsNcontrib", "", kTH2F, {axisNcontrib, axisColTimeRes});
    histos.add("hColTimeResVsNcontribITSonly", "", kTH2F, {axisNcontrib, axisColTimeRes});
    histos.add("hColTimeResVsNcontribWithTOF", "", kTH2F, {axisNcontrib, axisColTimeRes});
    histos.add("hColTimeResVsNcontribWithTRD", "", kTH2F, {axisNcontrib, axisColTimeRes});
    histos.add("hColBcDiffVsNcontrib", "", kTH2F, {axisNcontrib, axisBcDif});
    histos.add("hColBcDiffVsNcontribITSonly", "", kTH2F, {axisNcontrib, axisBcDif});
    histos.add("hColBcDiffVsNcontribWithTOF", "", kTH2F, {axisNcontrib, axisBcDif});
    histos.add("hColBcDiffVsNcontribWithTRD", "", kTH2F, {axisNcontrib, axisBcDif});

    histos.add("hITStrackBcDiff", "", kTH1F, {axisBcDif});
    histos.add("hTrackBcDiffVsEta", "", kTH2F, {axisEta, axisBcDif});
    histos.add("hTrackBcDiffVsEtaAll", "", kTH2F, {axisEta, axisBcDif});

    histos.add("hNcontribCol", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribAcc", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribMis", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribColTOF", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribColTRD", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribAccTOF", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribAccTRD", "", kTH1F, {axisNcontrib});
    histos.add("hNcontribMisTOF", "", kTH1F, {axisNcontrib});

    histos.add("hMultT0MVsNcontribAcc", "", kTH2F, {axisMultT0M, axisNcontrib}); // before ITS RO Frame border cut
    histos.add("hMultT0MVsNcontribCut", "", kTH2F, {axisMultT0M, axisNcontrib}); // after ITS RO Frame border cut

    histos.add("hMultV0AVsNcontribAcc", "", kTH2F, {axisMultV0A, axisNcontrib});         // before ITS RO Frame border cut
    histos.add("hMultV0AVsNcontribCut", "", kTH2F, {axisMultV0A, axisNcontrib});         // after ITS RO Frame border cut
    histos.add("hMultV0AVsNcontribAfterVertex", "", kTH2F, {axisMultV0A, axisNcontrib}); // after good vertex cut
    histos.add("hMultV0AVsNcontribGood", "", kTH2F, {axisMultV0A, axisNcontrib});        // after pileup check

    histos.add("hBcForMultV0AVsNcontribAcc", "", kTH1F, {axisBCs});      // bc distribution for V0A-vs-Ncontrib accepted
    histos.add("hBcForMultV0AVsNcontribOutliers", "", kTH1F, {axisBCs}); // bc distribution for V0A-vs-Ncontrib outliers
    histos.add("hBcForMultV0AVsNcontribCut", "", kTH1F, {axisBCs});      // bc distribution for V0A-vs-Ncontrib after ITS-ROF border cut

    histos.add("hVtxFT0VsVtxCol", "", kTH2F, {axisVtxZ, axisVtxZ});                // FT0-vertex vs z-vertex from collisions
    histos.add("hVtxFT0MinusVtxCol", "", kTH1F, {axisVtxZ});                       // FT0-vertex minus z-vertex from collisions
    histos.add("hVtxFT0MinusVtxColVsMultT0M", "", kTH2F, {axisVtxZ, axisMultT0M}); // FT0-vertex minus z-vertex from collisions vs multiplicity

    histos.add("hFoundBc", "", kTH1F, {axisBCs});            // distribution of found bcs (for ITS ROF studies)
    histos.add("hFoundBcTOF", "", kTH1F, {axisBCs});         // distribution of found bcs (TOF-matched vertex)
    histos.add("hFoundBcNcontrib", "", kTH1F, {axisBCs});    // accumulated distribution of n contributors vs found bc (for ITS ROF studies)
    histos.add("hFoundBcNcontribTOF", "", kTH1F, {axisBCs}); // accumulated distribution of n contributors vs found bc (TOF-matched vertex)

    // MC histograms
    histos.add("hGlobalBcColMC", "", kTH1F, {axisGlobalBCs});
    histos.add("hBcColMC", "", kTH1F, {axisBCs});
    histos.add("hVertexXMC", "", kTH1F, {axisVtxXY});
    histos.add("hVertexYMC", "", kTH1F, {axisVtxXY});
    histos.add("hVertexZMC", "", kTH1F, {axisVtxZ});

    for (int i = 0; i < kNsel; i++) {
      histos.get<TH1>(HIST("hSelCounter"))->GetXaxis()->SetBinLabel(i + 1, selectionLabels[i]);
      histos.get<TH1>(HIST("hSelMask"))->GetXaxis()->SetBinLabel(i + 1, selectionLabels[i]);
    }
    for (int i = 0; i < kNaliases; i++) {
      histos.get<TH1>(HIST("hColCounterAll"))->GetXaxis()->SetBinLabel(i + 1, aliasLabels[i].data());
      histos.get<TH1>(HIST("hColCounterAcc"))->GetXaxis()->SetBinLabel(i + 1, aliasLabels[i].data());
      histos.get<TH1>(HIST("hBcCounterAll"))->GetXaxis()->SetBinLabel(i + 1, aliasLabels[i].data());
    }
  }

  void processRun2(
    ColEvSels const& cols,
    BCsRun2 const& bcs,
    aod::Zdcs const&,
    aod::FV0As const&,
    aod::FV0Cs const&,
    aod::FT0s const&,
    aod::FDDs const&)
  {
    bool isINT1period = 0;
    if (!applySelection) {
      auto first_bc = bcs.iteratorAt(0);
      EventSelectionParams* par = ccdb->getForTimeStamp<EventSelectionParams>("EventSelection/EventSelectionParams", first_bc.timestamp());
      applySelection = par->GetSelection(0);
      for (int i = 0; i < kNsel; i++) {
        histos.get<TH1>(HIST("hSelMask"))->SetBinContent(i + 1, applySelection[i]);
      }
      isINT1period = first_bc.runNumber() <= 136377 || (first_bc.runNumber() >= 144871 && first_bc.runNumber() <= 159582);
    }

    // bc-based event selection qa
    for (const auto& bc : bcs) {
      for (int iAlias = 0; iAlias < kNaliases; iAlias++) {
        histos.fill(HIST("hBcCounterAll"), iAlias, bc.alias_bit(iAlias));
      }
    }

    // collision-based event selection qa
    for (const auto& col : cols) {
      bool sel1 = col.selection_bit(kIsINT1) && col.selection_bit(kNoBGV0A) && col.selection_bit(kNoBGV0C) && col.selection_bit(kNoTPCLaserWarmUp) && col.selection_bit(kNoTPCHVdip);

      for (int iAlias = 0; iAlias < kNaliases; iAlias++) {
        if (!col.alias_bit(iAlias)) {
          continue;
        }
        histos.fill(HIST("hColCounterAll"), iAlias, 1);
        if ((!isINT1period && col.sel7()) || (isINT1period && sel1)) {
          histos.fill(HIST("hColCounterAcc"), iAlias, 1);
        }
      }

      bool mb = isMC;
      mb |= !isINT1period && col.alias_bit(kINT7);
      mb |= isINT1period && col.alias_bit(kINT1);
      // further checks just on minimum bias triggers
      if (!mb) {
        continue;
      }
      for (int i = 0; i < kNsel; i++) {
        histos.fill(HIST("hSelCounter"), i, col.selection_bit(i));
      }

      const auto& bc = col.bc_as<BCsRun2>();
      uint64_t globalBC = bc.globalBC();
      // uint64_t orbit = globalBC / nBCsPerOrbit;
      int localBC = globalBC % nBCsPerOrbit;
      histos.fill(HIST("hGlobalBcAll"), globalBC - minGlobalBC);
      // histos.fill(HIST("hOrbitAll"), orbit - minOrbit);
      histos.fill(HIST("hBcAll"), localBC);
      if (col.selection_bit(kIsBBV0A) || col.selection_bit(kIsBBV0C)) {
        histos.fill(HIST("hGlobalBcFV0"), globalBC - minGlobalBC);
        // histos.fill(HIST("hOrbitFV0"), orbit - minOrbit);
        histos.fill(HIST("hBcFV0"), localBC);
      }
      if (col.selection_bit(kIsBBT0A) || col.selection_bit(kIsBBT0C)) {
        histos.fill(HIST("hGlobalBcFT0"), globalBC - minGlobalBC);
        // histos.fill(HIST("hOrbitFT0"), orbit - minOrbit);
        histos.fill(HIST("hBcFT0"), localBC);
      }
      if (col.selection_bit(kIsBBFDA) || col.selection_bit(kIsBBFDC)) {
        histos.fill(HIST("hGlobalBcFDD"), globalBC - minGlobalBC);
        // histos.fill(HIST("hOrbitFDD"), orbit - minOrbit);
        histos.fill(HIST("hBcFDD"), localBC);
      }

      // Calculate V0 multiplicity per ring
      float multRingV0A[5] = {0.};
      float multRingV0C[4] = {0.};
      float multV0A = 0;
      float multV0C = 0;
      if (bc.has_fv0a()) {
        for (unsigned int i = 0; i < bc.fv0a().amplitude().size(); ++i) {
          int ring = bc.fv0a().channel()[i] / 8;
          multRingV0A[ring] += bc.fv0a().amplitude()[i];
          multV0A += bc.fv0a().amplitude()[i];
        }
      }

      if (bc.has_fv0c()) {
        for (unsigned int i = 0; i < bc.fv0c().amplitude().size(); ++i) {
          int ring = bc.fv0c().channel()[i] / 8;
          multRingV0C[ring] += bc.fv0c().amplitude()[i];
          multV0C += bc.fv0c().amplitude()[i];
        }
      }

      float timeZNA = bc.has_zdc() ? bc.zdc().timeZNA() : -999.f;
      float timeZNC = bc.has_zdc() ? bc.zdc().timeZNC() : -999.f;
      float timeV0A = bc.has_fv0a() ? bc.fv0a().time() : -999.f;
      float timeV0C = bc.has_fv0c() ? bc.fv0c().time() : -999.f;
      float timeT0A = bc.has_ft0() ? bc.ft0().timeA() : -999.f;
      float timeT0C = bc.has_ft0() ? bc.ft0().timeC() : -999.f;
      float timeFDA = bc.has_fdd() ? bc.fdd().timeA() : -999.f;
      float timeFDC = bc.has_fdd() ? bc.fdd().timeC() : -999.f;
      float znSum = timeZNA + timeZNC;
      float znDif = timeZNA - timeZNC;
      float ofSPD = bc.spdFiredChipsL0() + bc.spdFiredChipsL1();
      float onSPD = bc.spdFiredFastOrL0() + bc.spdFiredFastOrL1();
      float multV0M = multV0A + multV0C;
      float multRingV0C3 = multRingV0C[3];
      float multRingV0C012 = multV0C - multRingV0C3;
      float onV0M = bc.v0TriggerChargeA() + bc.v0TriggerChargeC();
      float ofV0M = multV0A + multV0C - multRingV0A[0];
      int spdClusters = bc.spdClustersL0() + bc.spdClustersL1();

      auto trackletsGrouped = tracklets->sliceByCached(aod::track::collisionId, col.globalIndex(), cache);
      int nTracklets = trackletsGrouped.size();

      float multFDA = 0;
      float multFDC = 0;
      float multT0A = bc.has_ft0() ? bc.ft0().sumAmpA() : -999.f;
      float multT0C = bc.has_ft0() ? bc.ft0().sumAmpC() : -999.f;

      if (bc.has_fdd()) {
        for (auto amplitude : bc.fdd().chargeA()) {
          multFDA += amplitude;
        }
        for (auto amplitude : bc.fdd().chargeC()) {
          multFDC += amplitude;
        }
      }
      float multZNA = bc.has_zdc() ? bc.zdc().energyCommonZNA() : 0;
      float multZNC = bc.has_zdc() ? bc.zdc().energyCommonZNC() : 0;

      histos.fill(HIST("hMultV0Mcol"), multV0M);
      histos.fill(HIST("hMultV0Acol"), multV0A);
      histos.fill(HIST("hMultV0Ccol"), multV0C);
      histos.fill(HIST("hMultZNAcol"), multZNA);
      histos.fill(HIST("hMultZNCcol"), multZNC);
      histos.fill(HIST("hMultT0Acol"), multT0A);
      histos.fill(HIST("hMultT0Ccol"), multT0C);
      histos.fill(HIST("hMultFDAcol"), multFDA);
      histos.fill(HIST("hMultFDCcol"), multFDC);

      histos.fill(HIST("hTimeV0Acol"), timeV0A);
      histos.fill(HIST("hTimeV0Ccol"), timeV0C);
      histos.fill(HIST("hTimeZNAcol"), timeZNA);
      histos.fill(HIST("hTimeZNCcol"), timeZNC);
      histos.fill(HIST("hTimeT0Acol"), timeT0A);
      histos.fill(HIST("hTimeT0Ccol"), timeT0C);
      histos.fill(HIST("hTimeFDAcol"), timeFDA);
      histos.fill(HIST("hTimeFDCcol"), timeFDC);
      histos.fill(HIST("hTimeZACcol"), znDif, znSum);
      histos.fill(HIST("hSPDClsVsTklCol"), nTracklets, spdClusters);
      histos.fill(HIST("hSPDOnVsOfCol"), ofSPD, onSPD);
      histos.fill(HIST("hV0MOnVsOfCol"), ofV0M, onV0M);
      histos.fill(HIST("hV0C3vs012Col"), multRingV0C012, multRingV0C3);
      histos.fill(HIST("hV0C012vsTklCol"), nTracklets, multRingV0C012);

      // filling plots for accepted events
      bool accepted = 0;
      accepted |= !isINT1period & col.sel7();
      accepted |= isINT1period & sel1;
      if (!accepted) {
        continue;
      }

      histos.fill(HIST("hMultV0Macc"), multV0M);
      histos.fill(HIST("hMultV0Aacc"), multV0A);
      histos.fill(HIST("hMultV0Cacc"), multV0C);
      histos.fill(HIST("hMultZNAacc"), multZNA);
      histos.fill(HIST("hMultZNCacc"), multZNC);
      histos.fill(HIST("hMultT0Aacc"), multT0A);
      histos.fill(HIST("hMultT0Cacc"), multT0C);
      histos.fill(HIST("hMultFDAacc"), multFDA);
      histos.fill(HIST("hMultFDCacc"), multFDC);

      histos.fill(HIST("hTimeV0Aacc"), timeV0A);
      histos.fill(HIST("hTimeV0Cacc"), timeV0C);
      histos.fill(HIST("hTimeZNAacc"), timeZNA);
      histos.fill(HIST("hTimeZNCacc"), timeZNC);
      histos.fill(HIST("hTimeT0Aacc"), timeT0A);
      histos.fill(HIST("hTimeT0Cacc"), timeT0C);
      histos.fill(HIST("hTimeFDAacc"), timeFDA);
      histos.fill(HIST("hTimeFDCacc"), timeFDC);
      histos.fill(HIST("hTimeZACacc"), znDif, znSum);
      histos.fill(HIST("hSPDClsVsTklAcc"), nTracklets, spdClusters);
      histos.fill(HIST("hSPDOnVsOfAcc"), ofSPD, onSPD);
      histos.fill(HIST("hV0MOnVsOfAcc"), ofV0M, onV0M);
      histos.fill(HIST("hV0C3vs012Acc"), multRingV0C012, multRingV0C3);
      histos.fill(HIST("hV0C012vsTklAcc"), nTracklets, multRingV0C012);
    }
  }
  PROCESS_SWITCH(EventSelectionQaTask, processRun2, "Process Run2 event selection QA", true);

  Preslice<FullTracksIU> perCollision = aod::track::collisionId;
  // Preslice<ColEvSels> perFoundBC = aod::evsel::foundBCId;

  void processRun3(
    ColEvSels const& cols,
    FullTracksIU const& tracks,
    aod::AmbiguousTracks const& ambTracks,
    BCsRun3 const& bcs,
    aod::Zdcs const&,
    aod::FV0As const&,
    aod::FT0s const&,
    aod::FDDs const&)
  {
    int runNumber = bcs.iteratorAt(0).runNumber();
    uint32_t nOrbitsPerTF = 128; // 128 in 2022, 32 in 2023
    if (runNumber != lastRunNumber) {
      lastRunNumber = runNumber; // do it only once
      int64_t tsSOR = 0;
      int64_t tsEOR = 1;

      if (runNumber >= 500000) { // access CCDB for data or anchored MC only
        int64_t ts = bcs.iteratorAt(0).timestamp();

        // access colliding and beam-gas bc patterns
        auto grplhcif = ccdb->getForTimeStamp<o2::parameters::GRPLHCIFData>("GLO/Config/GRPLHCIF", ts);
        beamPatternA = grplhcif->getBunchFilling().getBeamPattern(0);
        beamPatternC = grplhcif->getBunchFilling().getBeamPattern(1);
        bcPatternA = beamPatternA & ~beamPatternC;
        bcPatternC = ~beamPatternA & beamPatternC;
        bcPatternB = beamPatternA & beamPatternC;

        for (int i = 0; i < nBCsPerOrbit; i++) {
          if (bcPatternA[i]) {
            histos.fill(HIST("hBcA"), i);
          }
          if (bcPatternC[i]) {
            histos.fill(HIST("hBcC"), i);
          }
          if (bcPatternB[i]) {
            histos.fill(HIST("hBcB"), i);
          }
        }

        EventSelectionParams* par = ccdb->getForTimeStamp<EventSelectionParams>("EventSelection/EventSelectionParams", ts);
        // access orbit-reset timestamp
        auto ctpx = ccdb->getForTimeStamp<std::vector<Long64_t>>("CTP/Calib/OrbitReset", ts);
        int64_t tsOrbitReset = (*ctpx)[0]; // us
        // access TF duration, start-of-run and end-of-run timestamps from ECS GRP
        std::map<std::string, std::string> metadata;
        metadata["runNumber"] = Form("%d", runNumber);
        auto grpecs = ccdb->getSpecific<o2::parameters::GRPECSObject>("GLO/Config/GRPECS", ts, metadata);
        nOrbitsPerTF = grpecs->getNHBFPerTF(); // assuming 1 orbit = 1 HBF;  nOrbitsPerTF=128 in 2022, 32 in 2023
        tsSOR = grpecs->getTimeStart();        // ms
        tsEOR = grpecs->getTimeEnd();          // ms
        // calculate SOR and EOR orbits
        int64_t orbitSOR = (tsSOR * 1000 - tsOrbitReset) / o2::constants::lhc::LHCOrbitMUS;
        int64_t orbitEOR = (tsEOR * 1000 - tsOrbitReset) / o2::constants::lhc::LHCOrbitMUS;
        // adjust to the nearest TF edge
        orbitSOR = orbitSOR / nOrbitsPerTF * nOrbitsPerTF + par->fTimeFrameOrbitShift;
        orbitEOR = orbitEOR / nOrbitsPerTF * nOrbitsPerTF + par->fTimeFrameOrbitShift;
        // set nOrbits and minOrbit used for orbit-axis binning
        nOrbits = orbitEOR - orbitSOR;
        minOrbit = orbitSOR;
        // first bc of the first orbit (should coincide with TF start)
        bcSOR = orbitSOR * o2::constants::lhc::LHCMaxBunches;
        // duration of TF in bcs
        nBCsPerTF = nOrbitsPerTF * o2::constants::lhc::LHCMaxBunches;
        LOGP(info, "tsOrbitReset={} us, SOR = {} ms, EOR = {} ms, orbitSOR = {}, nBCsPerTF = {}", tsOrbitReset, tsSOR, tsEOR, orbitSOR, nBCsPerTF);
      }

      // create orbit-axis histograms on the fly with binning based on info from GRP if GRP is available
      // otherwise default minOrbit and nOrbits will be used
      const AxisSpec axisOrbits{static_cast<int>(nOrbits / nOrbitsPerTF), 0., static_cast<double>(nOrbits), ""};
      histos.add("hOrbitAll", "", kTH1F, {axisOrbits});
      histos.add("hOrbitCol", "", kTH1F, {axisOrbits});
      histos.add("hOrbitAcc", "", kTH1F, {axisOrbits});
      histos.add("hOrbitTVX", "", kTH1F, {axisOrbits});
      histos.add("hOrbitFT0", "", kTH1F, {axisOrbits});
      histos.add("hOrbitFV0", "", kTH1F, {axisOrbits});
      histos.add("hOrbitFDD", "", kTH1F, {axisOrbits});
      histos.add("hOrbitZDC", "", kTH1F, {axisOrbits});
      histos.add("hOrbitColMC", "", kTH1F, {axisOrbits});

      const AxisSpec axisBCinTF{static_cast<int>(nBCsPerTF), 0, static_cast<double>(nBCsPerTF), "bc in TF"};
      histos.add("hNcontribVsBcInTF", ";bc in TF; n vertex contributors", kTH1F, {axisBCinTF});
      histos.add("hNcontribAfterCutsVsBcInTF", ";bc in TF; n vertex contributors", kTH1F, {axisBCinTF});
      histos.add("hNcolMCVsBcInTF", ";bc in TF; n MC collisions", kTH1F, {axisBCinTF});
      histos.add("hNcolVsBcInTF", ";bc in TF; n collisions", kTH1F, {axisBCinTF});
      histos.add("hNtvxVsBcInTF", ";bc in TF; n TVX triggers", kTH1F, {axisBCinTF});

      double minSec = floor(tsSOR / 1000.);
      double maxSec = ceil(tsEOR / 1000.);
      const AxisSpec axisSeconds{static_cast<int>(maxSec - minSec), minSec, maxSec, "seconds"};
      const AxisSpec axisBcDif{600, -300., 300., "bc difference"};
      histos.add("hSecondsTVXvsBcDif", "", kTH2F, {axisSeconds, axisBcDif});
      histos.add("hSecondsTVXvsBcDifAll", "", kTH2F, {axisSeconds, axisBcDif});
    }

    // background studies
    for (const auto& bc : bcs) {
      // make sure previous bcs are empty to clean-up other activity
      uint64_t globalBC = bc.globalBC();
      int deltaIndex = 0;  // backward move counts
      int deltaBC = 0;     // current difference wrt globalBC
      int maxDeltaBC = 10; // maximum difference
      bool pastActivityFT0 = 0;
      bool pastActivityFDD = 0;
      bool pastActivityFV0 = 0;
      while (deltaBC < maxDeltaBC) {
        if (bc.globalIndex() - deltaIndex < 0) {
          break;
        }
        deltaIndex++;
        const auto& bc_past = bcs.iteratorAt(bc.globalIndex() - deltaIndex);
        deltaBC = globalBC - bc_past.globalBC();
        if (deltaBC < maxDeltaBC) {
          pastActivityFT0 |= bc_past.has_ft0();
          pastActivityFV0 |= bc_past.has_fv0a();
          pastActivityFDD |= bc_past.has_fdd();
        }
      }

      bool pastActivity = pastActivityFT0 | pastActivityFV0 | pastActivityFDD;

      int localBC = bc.globalBC() % nBCsPerOrbit;
      float timeV0A = bc.has_fv0a() ? bc.fv0a().time() : -999.f;
      float timeT0A = bc.has_ft0() ? bc.ft0().timeA() : -999.f;
      float timeT0C = bc.has_ft0() ? bc.ft0().timeC() : -999.f;
      float timeFDA = bc.has_fdd() ? bc.fdd().timeA() : -999.f;
      float timeFDC = bc.has_fdd() ? bc.fdd().timeC() : -999.f;
      if (bcPatternA[(localBC + 5) % nBCsPerOrbit] && !pastActivity && !bc.has_ft0()) {
        histos.fill(HIST("hTimeFDAbga"), timeFDA);
        histos.fill(HIST("hTimeFDCbga"), timeFDC);
      }
      if (bcPatternC[(localBC + 5) % nBCsPerOrbit] && !pastActivity && !bc.has_ft0()) {
        histos.fill(HIST("hTimeFDAbgc"), timeFDA);
        histos.fill(HIST("hTimeFDCbgc"), timeFDC);
      }
      if (bcPatternA[(localBC + 1) % nBCsPerOrbit] && !pastActivity && !bc.has_ft0()) {
        histos.fill(HIST("hTimeT0Abga"), timeT0A);
        histos.fill(HIST("hTimeT0Cbga"), timeT0C);
        histos.fill(HIST("hTimeV0Abga"), timeV0A);
      }
      if (bcPatternC[(localBC + 1) % nBCsPerOrbit] && !pastActivity && !bc.has_ft0()) {
        histos.fill(HIST("hTimeT0Abgc"), timeT0A);
        histos.fill(HIST("hTimeT0Cbgc"), timeT0C);
      }
    }

    // vectors of TVX flags used for past-future studies
    int nBCs = bcs.size();
    std::vector<bool> vIsTVX(nBCs, 0);
    std::vector<uint64_t> vGlobalBCs(nBCs, 0);

    // bc-based event selection qa
    for (const auto& bc : bcs) {
      if (!bc.has_ft0())
        continue;
      float multT0A = bc.ft0().sumAmpA();
      float multT0C = bc.ft0().sumAmpC();
      histos.fill(HIST("hMultT0Mref"), multT0A + multT0C);
      if (!bc.selection_bit(kIsTriggerTVX))
        continue;
      histos.fill(HIST("hMultT0Mtvx"), multT0A + multT0C);
      histos.fill(HIST("hMultT0Atvx"), multT0A);
      histos.fill(HIST("hMultT0Ctvx"), multT0C);
      if (!bc.selection_bit(kIsBBZAC))
        continue;
      histos.fill(HIST("hMultT0Mzac"), multT0A + multT0C);
      histos.fill(HIST("hMultT0Azac"), multT0A);
      histos.fill(HIST("hMultT0Czac"), multT0C);
    }

    // bc-based event selection qa
    for (const auto& bc : bcs) {
      for (int iAlias = 0; iAlias < kNaliases; iAlias++) {
        histos.fill(HIST("hBcCounterAll"), iAlias, bc.alias_bit(iAlias));
      }
      uint64_t globalBC = bc.globalBC();
      uint64_t orbit = globalBC / nBCsPerOrbit;
      int localBC = globalBC % nBCsPerOrbit;
      float timeZNA = bc.has_zdc() ? bc.zdc().timeZNA() : -999.f;
      float timeZNC = bc.has_zdc() ? bc.zdc().timeZNC() : -999.f;
      float timeV0A = bc.has_fv0a() ? bc.fv0a().time() : -999.f;
      float timeT0A = bc.has_ft0() ? bc.ft0().timeA() : -999.f;
      float timeT0C = bc.has_ft0() ? bc.ft0().timeC() : -999.f;
      float timeFDA = bc.has_fdd() ? bc.fdd().timeA() : -999.f;
      float timeFDC = bc.has_fdd() ? bc.fdd().timeC() : -999.f;
      histos.fill(HIST("hTimeV0Aall"), timeV0A);
      histos.fill(HIST("hTimeZNAall"), timeZNA);
      histos.fill(HIST("hTimeZNCall"), timeZNC);
      histos.fill(HIST("hTimeT0Aall"), timeT0A);
      histos.fill(HIST("hTimeT0Call"), timeT0C);
      histos.fill(HIST("hTimeFDAall"), timeFDA);
      histos.fill(HIST("hTimeFDCall"), timeFDC);
      if (bcPatternB[localBC]) {
        histos.fill(HIST("hTimeV0Aref"), timeV0A);
        histos.fill(HIST("hTimeZNAref"), timeZNA);
        histos.fill(HIST("hTimeZNCref"), timeZNC);
        histos.fill(HIST("hTimeT0Aref"), timeT0A);
        histos.fill(HIST("hTimeT0Cref"), timeT0C);
        histos.fill(HIST("hTimeFDAref"), timeFDA);
        histos.fill(HIST("hTimeFDCref"), timeFDC);
      }

      histos.fill(HIST("hGlobalBcAll"), globalBC - minGlobalBC);
      histos.fill(HIST("hOrbitAll"), orbit - minOrbit);
      histos.fill(HIST("hBcAll"), localBC);

      if (bc.selection_bit(kIsTriggerTVX)) {
        histos.fill(HIST("hOrbitTVX"), orbit - minOrbit);
        histos.fill(HIST("hBcTVX"), localBC);
      }

      // FV0
      if (bc.has_fv0a()) {
        histos.fill(HIST("hGlobalBcFV0"), globalBC - minGlobalBC);
        histos.fill(HIST("hOrbitFV0"), orbit - minOrbit);
        histos.fill(HIST("hBcFV0"), localBC);
        float multV0A = 0;
        for (auto amplitude : bc.fv0a().amplitude()) {
          multV0A += amplitude;
        }
        histos.fill(HIST("hMultV0Aall"), multV0A);
        if (bcPatternB[localBC]) {
          histos.fill(HIST("hMultV0Aref"), multV0A);
        }
      }

      // FT0
      if (bc.has_ft0()) {
        histos.fill(HIST("hGlobalBcFT0"), globalBC - minGlobalBC);
        histos.fill(HIST("hOrbitFT0"), orbit - minOrbit);
        histos.fill(HIST("hBcFT0"), localBC);
        float multT0A = bc.ft0().sumAmpA();
        float multT0C = bc.ft0().sumAmpC();
        histos.fill(HIST("hMultT0Aall"), multT0A);
        histos.fill(HIST("hMultT0Call"), multT0C);
        if (bcPatternB[localBC]) {
          histos.fill(HIST("hMultT0Aref"), multT0A);
          histos.fill(HIST("hMultT0Cref"), multT0C);
        }
        if (bc.selection_bit(kIsTriggerTVX)) {
          int64_t bcInTF = (globalBC - bcSOR) % nBCsPerTF;
          histos.fill(HIST("hNtvxVsBcInTF"), bcInTF);
        }
        if (!bc.selection_bit(kNoBGFDA) && bc.selection_bit(kIsTriggerTVX)) {
          histos.fill(HIST("hMultT0Abga"), multT0A);
          histos.fill(HIST("hMultT0Cbga"), multT0C);
        }
        if (!bc.selection_bit(kNoBGFDC) && bc.selection_bit(kIsTriggerTVX)) {
          histos.fill(HIST("hMultT0Abgc"), multT0A);
          histos.fill(HIST("hMultT0Cbgc"), multT0C);
        }
      }

      // FDD
      if (bc.has_fdd()) {
        histos.fill(HIST("hGlobalBcFDD"), globalBC - minGlobalBC);
        histos.fill(HIST("hOrbitFDD"), orbit - minOrbit);
        histos.fill(HIST("hBcFDD"), localBC);
        float multFDA = 0;
        for (auto amplitude : bc.fdd().chargeA()) {
          multFDA += amplitude;
        }
        float multFDC = 0;
        for (auto amplitude : bc.fdd().chargeC()) {
          multFDC += amplitude;
        }
        histos.fill(HIST("hMultFDAall"), multFDA);
        histos.fill(HIST("hMultFDCall"), multFDC);
        if (bcPatternB[localBC]) {
          histos.fill(HIST("hMultFDAref"), multFDA);
          histos.fill(HIST("hMultFDCref"), multFDC);
        }
      }

      // ZDC
      if (bc.has_zdc()) {
        histos.fill(HIST("hGlobalBcZDC"), globalBC - minGlobalBC);
        histos.fill(HIST("hOrbitZDC"), orbit - minOrbit);
        histos.fill(HIST("hBcZDC"), localBC);
        float multZNA = bc.zdc().energyCommonZNA();
        float multZNC = bc.zdc().energyCommonZNC();
        histos.fill(HIST("hMultZNAall"), multZNA);
        histos.fill(HIST("hMultZNCall"), multZNC);
        if (bcPatternB[localBC]) {
          histos.fill(HIST("hMultZNAref"), multZNA);
          histos.fill(HIST("hMultZNCref"), multZNC);
        }
      }

      // fill TVX flags for past-future searches
      int indexBc = bc.globalIndex();
      vIsTVX[indexBc] = bc.selection_bit(kIsTriggerTVX);
      vGlobalBCs[indexBc] = globalBC;
    }

    // map for pileup checks
    std::vector<int> vCollisionsPerBc(bcs.size(), 0);
    for (const auto& col : cols) {
      if (col.foundBCId() < 0 || col.foundBCId() >= bcs.size())
        continue;
      vCollisionsPerBc[col.foundBCId()]++;
    }

    // build map from track index to ambiguous track index
    std::unordered_map<int32_t, int32_t> mapAmbTrIds;
    for (const auto& ambTrack : ambTracks) {
      mapAmbTrIds[ambTrack.trackId()] = ambTrack.globalIndex();
    }

    // create maps from globalBC to bc index for TVX or FT0-OR fired bcs
    // to be used for closest TVX (FT0-OR) searches
    std::map<int64_t, int32_t> mapGlobalBcWithTVX;
    std::map<int64_t, int32_t> mapGlobalBcWithTOR;
    for (const auto& bc : bcs) {
      int64_t globalBC = bc.globalBC();
      // skip non-colliding bcs for data and anchored runs
      if (runNumber >= 500000 && bcPatternB[globalBC % o2::constants::lhc::LHCMaxBunches] == 0) {
        continue;
      }
      if (bc.selection_bit(kIsBBT0A) || bc.selection_bit(kIsBBT0C)) {
        mapGlobalBcWithTOR[globalBC] = bc.globalIndex();
      }
      if (bc.selection_bit(kIsTriggerTVX)) {
        mapGlobalBcWithTVX[globalBC] = bc.globalIndex();
      }
    }

    // Fill track bc distributions (all tracks including ambiguous)
    for (const auto& track : tracks) {
      auto mapAmbTrIdsIt = mapAmbTrIds.find(track.globalIndex());
      int ambTrId = mapAmbTrIdsIt == mapAmbTrIds.end() ? -1 : mapAmbTrIdsIt->second;
      int indexBc = ambTrId < 0 ? track.collision_as<ColEvSels>().bc_as<BCsRun3>().globalIndex() : ambTracks.iteratorAt(ambTrId).bc_as<BCsRun3>().begin().globalIndex();
      auto bc = bcs.iteratorAt(indexBc);
      int64_t globalBC = bc.globalBC() + floor(track.trackTime() / o2::constants::lhc::LHCBunchSpacingNS);

      int32_t indexClosestTVX = findClosest(globalBC, mapGlobalBcWithTVX);
      int bcDiff = static_cast<int>(globalBC - vGlobalBCs[indexClosestTVX]);
      if (track.hasTOF() || track.hasTRD() || !track.hasITS() || !track.hasTPC() || track.pt() < 1)
        continue;
      histos.fill(HIST("hTrackBcDiffVsEtaAll"), track.eta(), bcDiff);
      if (track.eta() < -0.2 || track.eta() > 0.2)
        continue;
      histos.fill(HIST("hSecondsTVXvsBcDifAll"), bc.timestamp() / 1000., bcDiff);
    }

    // collision-based event selection qa
    for (const auto& col : cols) {
      for (int iAlias = 0; iAlias < kNaliases; iAlias++) {
        if (!col.alias_bit(iAlias)) {
          continue;
        }
        histos.fill(HIST("hColCounterAll"), iAlias, 1);
        if (!col.sel8()) {
          continue;
        }
        histos.fill(HIST("hColCounterAcc"), iAlias, 1);
      }

      for (int i = 0; i < kNsel; i++) {
        histos.fill(HIST("hSelCounter"), i, col.selection_bit(i));
      }

      auto bc = col.bc_as<BCsRun3>();
      uint64_t globalBC = bc.globalBC();
      uint64_t orbit = globalBC / nBCsPerOrbit;
      int localBC = globalBC % nBCsPerOrbit;
      histos.fill(HIST("hGlobalBcCol"), globalBC - minGlobalBC);
      histos.fill(HIST("hOrbitCol"), orbit - minOrbit);
      histos.fill(HIST("hBcCol"), localBC);
      if (col.sel8()) {
        histos.fill(HIST("hOrbitAcc"), orbit - minOrbit);
      }

      // search for nearest ft0a&ft0c entry
      int32_t indexClosestTVX = findClosest(globalBC, mapGlobalBcWithTVX);
      int bcDiff = static_cast<int>(globalBC - vGlobalBCs[indexClosestTVX]);

      // count tracks of different types
      auto tracksGrouped = tracks.sliceBy(perCollision, col.globalIndex());
      int nContributorsAfterEtaTPCCuts = 0;
      for (const auto& track : tracksGrouped) {
        int trackBcDiff = bcDiff + track.trackTime() / o2::constants::lhc::LHCBunchSpacingNS;
        if (!track.isPVContributor())
          continue;
        if (fabs(track.eta()) < 0.8 && track.tpcNClsFound() > 80 && track.tpcNClsCrossedRows() > 100)
          nContributorsAfterEtaTPCCuts++;
        if (!track.hasTPC())
          histos.fill(HIST("hITStrackBcDiff"), trackBcDiff);
        if (track.hasTOF()) {
          histos.fill(HIST("hBcTrackTOF"), (globalBC + TMath::FloorNint(track.trackTime() / o2::constants::lhc::LHCBunchSpacingNS)) % nBCsPerOrbit);
        } else if (track.hasTRD()) {
          histos.fill(HIST("hBcTrackTRD"), (globalBC + TMath::Nint(track.trackTime() / o2::constants::lhc::LHCBunchSpacingNS)) % nBCsPerOrbit);
        }
        if (track.hasTOF() || track.hasTRD() || !track.hasITS() || !track.hasTPC() || track.pt() < 1)
          continue;
        histos.fill(HIST("hTrackBcDiffVsEta"), track.eta(), trackBcDiff);
        if (track.eta() < -0.2 || track.eta() > 0.2)
          continue;
        histos.fill(HIST("hSecondsTVXvsBcDif"), bc.timestamp() / 1000., trackBcDiff);
      }

      int nContributors = col.numContrib();
      float timeRes = col.collisionTimeRes();
      int64_t bcInTF = (globalBC - bcSOR) % nBCsPerTF;
      histos.fill(HIST("hNcontribCol"), nContributors);
      histos.fill(HIST("hNcontribVsBcInTF"), bcInTF, nContributors);
      histos.fill(HIST("hNcontribAfterCutsVsBcInTF"), bcInTF, nContributorsAfterEtaTPCCuts);
      histos.fill(HIST("hNcolVsBcInTF"), bcInTF);
      histos.fill(HIST("hColBcDiffVsNcontrib"), nContributors, bcDiff);
      histos.fill(HIST("hColTimeResVsNcontrib"), nContributors, timeRes);
      if (!col.selection_bit(kIsVertexITSTPC)) {
        histos.fill(HIST("hColBcDiffVsNcontribITSonly"), nContributors, bcDiff);
        histos.fill(HIST("hColTimeResVsNcontribITSonly"), nContributors, timeRes);
      }
      if (col.selection_bit(kIsVertexTOFmatched)) {
        histos.fill(HIST("hColBcDiffVsNcontribWithTOF"), nContributors, bcDiff);
        histos.fill(HIST("hColTimeResVsNcontribWithTOF"), nContributors, timeRes);
        histos.fill(HIST("hNcontribColTOF"), nContributors);
        histos.fill(HIST("hBcColTOF"), localBC);
        if (col.sel8()) {
          histos.fill(HIST("hNcontribAccTOF"), nContributors);
        }
      }
      if (col.selection_bit(kIsVertexTRDmatched)) {
        histos.fill(HIST("hColBcDiffVsNcontribWithTRD"), nContributors, bcDiff);
        histos.fill(HIST("hColTimeResVsNcontribWithTRD"), nContributors, timeRes);
        histos.fill(HIST("hNcontribColTRD"), nContributors);
        histos.fill(HIST("hBcColTRD"), localBC);
        if (col.sel8()) {
          histos.fill(HIST("hNcontribAccTRD"), nContributors);
        }
      }

      const auto& foundBC = col.foundBC_as<BCsRun3>();

      float timeZNA = foundBC.has_zdc() ? foundBC.zdc().timeZNA() : -999.f;
      float timeZNC = foundBC.has_zdc() ? foundBC.zdc().timeZNC() : -999.f;
      float timeV0A = foundBC.has_fv0a() ? foundBC.fv0a().time() : -999.f;
      float timeT0A = foundBC.has_ft0() ? foundBC.ft0().timeA() : -999.f;
      float timeT0C = foundBC.has_ft0() ? foundBC.ft0().timeC() : -999.f;
      float timeFDA = foundBC.has_fdd() ? foundBC.fdd().timeA() : -999.f;
      float timeFDC = foundBC.has_fdd() ? foundBC.fdd().timeC() : -999.f;
      float znSum = timeZNA + timeZNC;
      float znDif = timeZNA - timeZNC;

      histos.fill(HIST("hTimeV0Acol"), timeV0A);
      histos.fill(HIST("hTimeZNAcol"), timeZNA);
      histos.fill(HIST("hTimeZNCcol"), timeZNC);
      histos.fill(HIST("hTimeT0Acol"), timeT0A);
      histos.fill(HIST("hTimeT0Ccol"), timeT0C);
      histos.fill(HIST("hTimeFDAcol"), timeFDA);
      histos.fill(HIST("hTimeFDCcol"), timeFDC);
      histos.fill(HIST("hTimeZACcol"), znDif, znSum);

      // FT0
      float multT0A = foundBC.has_ft0() ? foundBC.ft0().sumAmpA() : -999.f;
      float multT0C = foundBC.has_ft0() ? foundBC.ft0().sumAmpC() : -999.f;

      // FV0
      float multV0A = 0;
      if (foundBC.has_fv0a()) {
        for (auto amplitude : foundBC.fv0a().amplitude()) {
          multV0A += amplitude;
        }
      }
      // FDD
      float multFDA = 0;
      float multFDC = 0;
      if (foundBC.has_fdd()) {
        for (auto amplitude : foundBC.fdd().chargeA()) {
          multFDA += amplitude;
        }
        for (auto amplitude : foundBC.fdd().chargeC()) {
          multFDC += amplitude;
        }
      }

      // ZDC
      float multZNA = foundBC.has_zdc() ? foundBC.zdc().energyCommonZNA() : -999.f;
      float multZNC = foundBC.has_zdc() ? foundBC.zdc().energyCommonZNC() : -999.f;

      histos.fill(HIST("hMultT0Acol"), multT0A);
      histos.fill(HIST("hMultT0Ccol"), multT0C);
      histos.fill(HIST("hMultV0Acol"), multV0A);
      histos.fill(HIST("hMultFDAcol"), multFDA);
      histos.fill(HIST("hMultFDCcol"), multFDC);
      histos.fill(HIST("hMultZNAcol"), multZNA);
      histos.fill(HIST("hMultZNCcol"), multZNC);

      // filling plots for events passing basic TVX selection
      if (!col.selection_bit(kIsTriggerTVX)) {
        continue;
      }

      // z-vertex from FT0 vs PV
      if (foundBC.has_ft0()) {
        histos.fill(HIST("hVtxFT0VsVtxCol"), foundBC.ft0().posZ(), col.posZ());
        histos.fill(HIST("hVtxFT0MinusVtxCol"), foundBC.ft0().posZ() - col.posZ());
        histos.fill(HIST("hVtxFT0MinusVtxColVsMultT0M"), foundBC.ft0().posZ() - col.posZ(), multT0A + multT0C);
      }

      int foundLocalBC = foundBC.globalBC() % nBCsPerOrbit;

      if (col.selection_bit(kNoTimeFrameBorder)) {
        histos.fill(HIST("hMultV0AVsNcontribAcc"), multV0A, nContributors);
        histos.fill(HIST("hBcForMultV0AVsNcontribAcc"), foundLocalBC);
        histos.fill(HIST("hFoundBc"), foundLocalBC);
        histos.fill(HIST("hFoundBcNcontrib"), foundLocalBC, nContributors);
        if (col.selection_bit(kIsVertexTOFmatched)) {
          histos.fill(HIST("hFoundBcTOF"), foundLocalBC);
          histos.fill(HIST("hFoundBcNcontribTOF"), foundLocalBC, nContributors);
        }
        if (nContributors < 0.043 * multV0A - 860) {
          histos.fill(HIST("hBcForMultV0AVsNcontribOutliers"), foundLocalBC);
        }
        if (col.selection_bit(kNoITSROFrameBorder)) {
          histos.fill(HIST("hMultV0AVsNcontribCut"), multV0A, nContributors);
          histos.fill(HIST("hBcForMultV0AVsNcontribCut"), foundLocalBC);
        }
      }

      if (col.selection_bit(kNoITSROFrameBorder)) {
        histos.fill(HIST("hMultT0MVsNcontribCut"), multT0A + multT0C, nContributors);
      }

      // filling plots for accepted events
      if (!col.sel8()) {
        continue;
      }

      if (col.selection_bit(kIsVertexITSTPC)) {
        histos.fill(HIST("hMultV0AVsNcontribAfterVertex"), multV0A, nContributors);
        if (col.selection_bit(kNoSameBunchPileup)) {
          histos.fill(HIST("hMultV0AVsNcontribGood"), multV0A, nContributors);
        }
      }

      if (!col.selection_bit(kNoSameBunchPileup)) {
        histos.fill(HIST("hMultT0Mpup"), multT0A + multT0C);
      }

      histos.fill(HIST("hMultT0MVsNcontribAcc"), multT0A + multT0C, nContributors);
      histos.fill(HIST("hTimeV0Aacc"), timeV0A);
      histos.fill(HIST("hTimeZNAacc"), timeZNA);
      histos.fill(HIST("hTimeZNCacc"), timeZNC);
      histos.fill(HIST("hTimeT0Aacc"), timeT0A);
      histos.fill(HIST("hTimeT0Cacc"), timeT0C);
      histos.fill(HIST("hTimeFDAacc"), timeFDA);
      histos.fill(HIST("hTimeFDCacc"), timeFDC);
      histos.fill(HIST("hTimeZACacc"), znDif, znSum);
      histos.fill(HIST("hMultT0Aacc"), multT0A);
      histos.fill(HIST("hMultT0Cacc"), multT0C);
      histos.fill(HIST("hMultV0Aacc"), multV0A);
      histos.fill(HIST("hMultFDAacc"), multFDA);
      histos.fill(HIST("hMultFDCacc"), multFDC);
      histos.fill(HIST("hMultZNAacc"), multZNA);
      histos.fill(HIST("hMultZNCacc"), multZNC);
      histos.fill(HIST("hNcontribAcc"), nContributors);

    } // collisions
  }
  PROCESS_SWITCH(EventSelectionQaTask, processRun3, "Process Run3 event selection QA", false);

  void processMCRun3(aod::McCollisions const& mcCols, soa::Join<aod::Collisions, aod::McCollisionLabels, aod::EvSels> const& cols, BCsRun3 const&, aod::FT0s const&)
  {
    for (const auto& mcCol : mcCols) {
      auto bc = mcCol.bc_as<BCsRun3>();
      uint64_t globalBC = bc.globalBC();
      uint64_t orbit = globalBC / nBCsPerOrbit;
      int localBC = globalBC % nBCsPerOrbit;
      int64_t bcInTF = (globalBC - bcSOR) % nBCsPerTF;
      histos.fill(HIST("hGlobalBcColMC"), globalBC - minGlobalBC);
      histos.fill(HIST("hOrbitColMC"), orbit - minOrbit);
      histos.fill(HIST("hBcColMC"), localBC);
      histos.fill(HIST("hVertexXMC"), mcCol.posX());
      histos.fill(HIST("hVertexYMC"), mcCol.posY());
      histos.fill(HIST("hVertexZMC"), mcCol.posZ());
      histos.fill(HIST("hNcolMCVsBcInTF"), bcInTF);
    }

    // check fraction of collisions matched to wrong bcs
    for (const auto& col : cols) {
      if (!col.has_mcCollision()) {
        continue;
      }
      uint64_t mcBC = col.mcCollision().bc_as<BCsRun3>().globalBC();
      uint64_t rcBC = col.foundBC_as<BCsRun3>().globalBC();
      if (mcBC != rcBC) {
        histos.fill(HIST("hNcontribMis"), col.numContrib());
        if (col.collisionTimeRes() < 12) {
          // ~ wrong bcs for collisions with T0F-matched tracks
          histos.fill(HIST("hNcontribMisTOF"), col.numContrib());
        }
      }
    }
  }
  PROCESS_SWITCH(EventSelectionQaTask, processMCRun3, "Process Run3 MC event selection QA", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    adaptAnalysisTask<EventSelectionQaTask>(cfgc)};
}
