/* --------------------------------------------------
   Copyright (C): OpenGATE Collaboration
   This software is distributed under the terms
   of the GNU Lesser General  Public Licence (LGPL)
   See LICENSE.md for further details
   ------------------------------------ -------------- */

#include "G4EmCalculator.hh"
#include "G4ParticleDefinition.hh"
#include "G4RandomTools.hh"
#include "G4RunManager.hh"
#include "G4Threading.hh"

#include "GateHelpers.h"
#include "GateHelpersDict.h"
#include "GateHelpersImage.h"
#include "GateMaterialMuHandler.h"
#include "GateTLEDoseActor.h"

#include <iostream>
#include <itkAddImageFilter.h>
#include <itkImageRegionIterator.h>
#include <vector>

G4Mutex SetPixelTLEMutex = G4MUTEX_INITIALIZER;

GateTLEDoseActor::GateTLEDoseActor(py::dict &user_info)
    : GateDoseActor(user_info) {
  fVoxelVolume = 0;
  // FIXME WARNING : no MT yet
  fMultiThreadReady = true;
}

void GateTLEDoseActor::InitializeUserInput(py::dict &user_info) {
  GateDoseActor::InitializeUserInput(user_info);
  fEnergyMin = py::cast<double>(user_info["energy_min"]);
  fEnergyMax = py::cast<double>(user_info["energy_max"]);
  auto database = py::cast<std::string>(user_info["database"]);
  fMaterialMuHandler = GateMaterialMuHandler::GetInstance(database, fEnergyMax);
}

void GateTLEDoseActor::InitializeCpp() { GateDoseActor::InitializeCpp(); }

void GateTLEDoseActor::BeginOfRunActionMasterThread(int run_id) {
  GateDoseActor::BeginOfRunActionMasterThread(run_id);
}

void GateTLEDoseActor::BeginOfRunAction(const G4Run *run) {
  GateDoseActor::BeginOfRunAction(run);
  auto s = cpp_edep_image->GetSpacing();
  fVoxelVolume = s[0] * s[1] * s[2];
}

void GateTLEDoseActor::BeginOfEventAction(const G4Event *event) {
  GateDoseActor::BeginOfEventAction(event);
}

void GateTLEDoseActor::PreUserTrackingAction(const G4Track *track) {
  auto &l = fThreadLocalData.Get();
  if (track->GetDefinition()->GetParticleName() == "gamma") {
    l.fIsTLEGamma = false;
    l.fLastTrackId = 1;
  } else {
    auto track_id = track->GetTrackID();
    if (track_id < l.fLastTrackId) {
      // if the track_id is lower than the lastTrack, it means all the following
      // tracks will be without TLE mode (tracks are processed LIFO), so we
      // reset fLastTrackId to allow all tertiary (or more) tracks
      l.fIsTLEGamma = false;
      l.fLastTrackId = 1;
    }
  }
}

void GateTLEDoseActor::SteppingAction(G4Step *step) {
  auto &l = fThreadLocalData.Get();
  // Update the track id if there is secondary in the current non TLE particle
  if (!l.fIsTLEGamma)
    l.fLastTrackId += step->GetSecondaryInCurrentStep()->size();

  // For non-gamma particle, no TLE
  if (step->GetTrack()->GetDefinition()->GetParticleName() != "gamma") {
    auto track_id = step->GetTrack()->GetTrackID();
    if (track_id > l.fLastTrackId) {
      // If the current track id is larger than the stored one: this is a
      // track from a TLE gamma, so we ignore all other particles.
      return;
    } else {
      // If not, the track count as a conventional DoseActor
      return GateDoseActor::SteppingAction(step);
    }
  }

  auto pre_step = step->GetPreStepPoint();
  double energy = pre_step->GetKineticEnergy();

  // For too high energy, no TLE
  if (energy > fEnergyMax) {
    return GateDoseActor::SteppingAction(step);
  }
  l.fIsTLEGamma = true;

  auto weight = step->GetTrack()->GetWeight();
  auto step_length = step->GetStepLength();
  auto density = pre_step->GetMaterial()->GetDensity();
  auto mu_en_over_rho = fMaterialMuHandler->GetMuEnOverRho(
      pre_step->GetMaterialCutsCouple(), energy);
  // (0.1 because length is in mm -> cm)
  auto edep = weight * 0.1 * energy * mu_en_over_rho * step_length * density /
              (CLHEP::g / CLHEP::cm3);

  // Kill photon below a given energy
  if (energy <= fEnergyMin) {
    edep = energy;
    step->GetTrack()->SetTrackStatus(fStopAndKill);
  }
  double dose = edep / density;

  // Get the voxel index and check if the step was within the 3D image
  G4ThreeVector position;
  bool isInside;
  Image3DType::IndexType index;
  GetVoxelPosition(step, position, isInside, index);
  if (isInside) {
    G4AutoLock mutex(&SetPixelTLEMutex); // mutex is bound to the if-scope
    if (fDoseFlag) {
      ImageAddValue<Image3DType>(cpp_dose_image, index, dose);
    }
    ImageAddValue<Image3DType>(cpp_edep_image, index, edep);
  }
}

void GateTLEDoseActor::EndOfRunAction(const G4Run *run) {
  GateDoseActor::EndOfRunAction(run);
}

int GateTLEDoseActor::EndOfRunActionMasterThread(int run_id) {
  return GateDoseActor::EndOfRunActionMasterThread(run_id);
}