/* --------------------------------------------------
   Copyright (C): OpenGATE Collaboration
   This software is distributed under the terms
   of the GNU Lesser General  Public Licence (LGPL)
   See LICENSE.md for further details
   -------------------------------------------------- */

#ifndef GateDigitizerAdderActor_h
#define GateDigitizerAdderActor_h

#include "../GateVActor.h"
#include "G4Cache.hh"
#include "GateDigiCollection.h"
#include "GateDigiCollectionIterator.h"
#include "GateHelpersDigitizer.h"
#include "GateTDigiAttribute.h"
#include <pybind11/stl.h>

namespace py = pybind11;

/*
 * Create a collection of "singles":
 *
 * - when every event ends, we consider all digi in the bottom most volume
 * - sum all deposited energy
 * - compute one single position, either the one the digi with the max energy
 *  (EnergyWinnerPosition) or the energy weighted position
 *  (EnergyWeightedCentroidPosition)
 *
 *  Warning: if the volume is composed of several sub volumes, digi will be
 *  grouped independently for all sub-volumes. This is determined thanks to the
 *  UniqueVolumeID.
 *
 *  Warning: digi are gathered per Event, not per time.
 *
 */

class GateDigiAdderInVolume;

class GateDigitizerAdderActor : public GateVActor {

public:
  enum AdderPolicy {
    Error,
    EnergyWinnerPosition,
    EnergyWeightedCentroidPosition
  };

  explicit GateDigitizerAdderActor(py::dict &user_info);

  ~GateDigitizerAdderActor() override;

  // Called when the simulation start (master thread only)
  void StartSimulationAction() override;

  // Called when the simulation end (master thread only)
  void EndSimulationAction() override;

  // Called every time a Run starts (all threads)
  void BeginOfRunAction(const G4Run *run) override;

  // Called every time an Event starts
  void BeginOfEventAction(const G4Event *event) override;

  // Called every time a Run ends (all threads)
  void EndOfRunAction(const G4Run *run) override;

  void EndOfSimulationWorkerAction(const G4Run * /*unused*/) override;

  // Called every time an Event ends (all threads)
  void EndOfEventAction(const G4Event *event) override;

  void SetGroupVolumeDepth(int depth);

protected:
  std::string fOutputFilename;
  std::string fInputDigiCollectionName;
  std::string fOutputDigiCollectionName;
  GateDigiCollection *fOutputDigiCollection;
  GateDigiCollection *fInputDigiCollection;
  AdderPolicy fPolicy;
  std::vector<std::string> fUserSkipDigiAttributeNames;
  int fClearEveryNEvents;
  int fGroupVolumeDepth;

  GateVDigiAttribute *fOutputEdepAttribute{};
  GateVDigiAttribute *fOutputPosAttribute{};
  GateVDigiAttribute *fOutputGlobalTimeAttribute{};

  void InitializeComputation();

  void AddDigiPerVolume();

  // During computation (thread local)
  struct threadLocalT {
    std::map<std::string, GateDigiAdderInVolume> fMapOfDigiInVolume;
    GateDigiAttributesFiller *fDigiAttributeFiller;
    GateDigiCollection::Iterator fInputIter;
    double *edep;
    G4ThreeVector *pos;
    GateUniqueVolumeID::Pointer *volID;
    double *time;
  };
  G4Cache<threadLocalT> fThreadLocalData;
};

#endif // GateDigitizerAdderActor_h