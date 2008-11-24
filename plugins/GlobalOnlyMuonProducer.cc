#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "DataFormats/Candidate/interface/Particle.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "DataFormats/MuonReco/interface/MuonFwd.h"
#include "DataFormats/MuonReco/interface/MuonCocktails.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/TrackToTrackMap.h"

using namespace std;
using namespace edm;
using namespace reco;

// Return the TeV-optimized refit track using the TMR choice; cuts on
// prob are old and are therefore unoptimized, but this is here for
// reference's sake.
//
// Function is in the same format as Piotr's in MuonCocktails.h.
reco::TrackRef tevOptimizedTMR( const reco::TrackRef& combinedTrack,
				const reco::TrackRef& trackerTrack,
				const reco::TrackToTrackMap tevMap1,
				const reco::TrackToTrackMap tevMap2,
				const reco::TrackToTrackMap tevMap3,
				const bool useTMR ) {

  enum { tk, gmr, fms, pmr };

  std::vector<reco::TrackRef> refit(4);
  bool ok[4];
  ok[tk] = true; // Assume tracker track OK.

  reco::TrackToTrackMap::const_iterator gmrTrack = tevMap1.find(combinedTrack);
  reco::TrackToTrackMap::const_iterator fmsTrack = tevMap2.find(combinedTrack);
  reco::TrackToTrackMap::const_iterator pmrTrack = tevMap3.find(combinedTrack);

  ok[gmr] = gmrTrack != tevMap1.end();
  ok[fms] = fmsTrack != tevMap2.end();
  ok[pmr] = pmrTrack != tevMap3.end();

  double prob[4];
  int chosen=3;

  if (ok[tk])  refit[tk]  = trackerTrack;
  if (ok[gmr]) refit[gmr] = (*gmrTrack).val;
  if (ok[fms]) refit[fms] = (*fmsTrack).val;
  if (ok[pmr]) refit[pmr] = (*pmrTrack).val;
  
  for (unsigned int i=0; i<4; i++) {
    prob[i] = (ok[i] && refit[i]->recHitsSize())
      ? muon::trackProbability(refit[i]) : 0.0; 
    if (prob[i] == 0.0) ok[i] = false;
  }

//  std::cout << "Probabilities: " << prob[0] << " " << prob[1] << " " << prob[2] << " " << prob[3] << std::endl;

  if (useTMR && ok[tk] && ok[fms]) {
    if (prob[fms] - prob[tk] > 30.)
      chosen = tk;
    else
      chosen = fms;
  }
  else if (ok[fms] && ok[pmr]) {
    if (prob[fms] - prob[pmr] > 0.9)
      chosen = pmr;
  }
  else {
    if      (ok[fms]) chosen = fms;
    else if (ok[pmr]) chosen = pmr;
  }
    
  return refit.at(chosen);
}

// Module to copy only the GlobalMuons out of the default
// MuonCollection (and ignore the Tracker, CaloMuons).

class GlobalOnlyMuonProducer : public EDProducer {
public:
  explicit GlobalOnlyMuonProducer(const ParameterSet&);
  ~GlobalOnlyMuonProducer() {}

private:
  virtual void beginJob(const EventSetup&) {}
  virtual void produce(Event&, const EventSetup&);
  virtual void endJob() {}

  // Store the track-to-track map(s) used when using TeV refit tracks.
  bool storeMatchMaps(const Event& event);

  // Take the muon passed in, clone it (so that we save all the muon
  // id information such as isolation, calo energy, etc.) and replace
  // its combined muon track with the passed in track.
  Muon* cloneAndSwitchTrack(const Muon& muon,
			    const TrackRef& newTrack) const;

  // The input (GMR) muons.
  InputTag src;

  // Allow building the muon from just the tracker track. This
  // functionality should go away after understanding the difference
  // between the output of option 1 of GlobalMuonProducer and just
  // looking at the tracker tracks of these muons.
  bool fromTrackerTrack;

  // If tevMuonTracks below is not "none", use the TeV refit track as
  // the combined track of the muon.
  bool fromTeVRefit;

  // Optionally switch out the combined muon track for one of the TeV
  // muon refit tracks, specified by the input tag here
  // (e.g. "tevMuons:firstHit").
  string tevMuonTracks;

  // Whether to make a cocktail muon instead of using just the one
  // type in tevMuonTracks.
  bool fromCocktail;
  
  // If we're not making cocktail muons, trackMap is the map that maps
  // global tracks to the desired TeV refit (e.g. from globalMuons to
  // tevMuons:picky).
  Handle<TrackToTrackMap> trackMap;

  // All the track maps used in making cocktail muons.
  Handle<TrackToTrackMap> trackMapDefault;
  Handle<TrackToTrackMap> trackMapFirstHit;
  Handle<TrackToTrackMap> trackMapPicky;
};

GlobalOnlyMuonProducer::GlobalOnlyMuonProducer(const ParameterSet& cfg)
  : src(cfg.getParameter<InputTag>("src")),
    fromTrackerTrack(cfg.getUntrackedParameter<bool>("fromTrackerTrack", false)),
    tevMuonTracks(cfg.getUntrackedParameter<string>("tevMuonTracks", "none")),
    fromCocktail(cfg.getUntrackedParameter<bool>("fromCocktail", false))
{
  fromTeVRefit = tevMuonTracks != "none";
  produces<MuonCollection>();
}

bool GlobalOnlyMuonProducer::storeMatchMaps(const Event& event) {
  if (fromCocktail) {
    event.getByLabel(tevMuonTracks, "default",  trackMapDefault);
    event.getByLabel(tevMuonTracks, "firstHit", trackMapFirstHit);
    event.getByLabel(tevMuonTracks, "picky",    trackMapPicky);
    return !trackMapDefault.failedToGet() && 
      !trackMapFirstHit.failedToGet() && !trackMapPicky.failedToGet();
  }
  else {
    event.getByLabel(InputTag(tevMuonTracks), trackMap);
    return !trackMap.failedToGet();
  }
}

Muon* GlobalOnlyMuonProducer::cloneAndSwitchTrack(const Muon& muon,
						  const TrackRef& newTrack) const {
  // Muon mass to make a four-vector out of the new track.
  static const double muMass = 0.10566;

  TrackRef tkTrack  = muon.track();
  TrackRef muTrack  = muon.standAloneMuon();
	  
  // Make up a real Muon from the tracker track.
  Particle::Point vtx(newTrack->vx(), newTrack->vy(), newTrack->vz());
  Particle::LorentzVector p4;
  double p = newTrack->p();
  p4.SetXYZT(newTrack->px(), newTrack->py(), newTrack->pz(),
	     sqrt(p*p + muMass*muMass));

  Muon* mu = muon.clone();
  mu->setCharge(newTrack->charge());
  mu->setP4(p4);
  mu->setVertex(vtx);
  mu->setGlobalTrack(newTrack);
  mu->setInnerTrack(tkTrack);
  mu->setOuterTrack(muTrack);
  return mu;
}

void GlobalOnlyMuonProducer::produce(Event& event, const EventSetup& eSetup) {
  // Get the global muons from the event.
  Handle<MuonCollection> muons;
  event.getByLabel(src, muons);

  // If we can't get the global muon collection, or below the
  // track-to-track maps needed, still produce an empty collection of
  // muons so consumers don't throw an exception.
  bool ok = !muons.failedToGet();

  // If we're instructed to use the TeV refit tracks in some way, we
  // need the track-to-track maps. If we're making a cocktail muon,
  // get all three track maps (the cocktail ingredients); else just
  // get the map which takes the above global tracks to the desired
  // TeV-muon refitted tracks (firstHit or picky).
  if (ok && fromTeVRefit)
    ok = storeMatchMaps(event);

  // Make the output collection.
  auto_ptr<MuonCollection> cands(new MuonCollection);

  if (ok) {
    MuonCollection::const_iterator muon;
    for (muon = muons->begin(); muon != muons->end(); muon++) {
      if (!muon->isGlobalMuon()) continue;

      if (fromTeVRefit) {
	// Start out with a null TrackRef.
	TrackRef tevTk;
      
	// If making a cocktail muon, use tevOptimized() to get the track
	// desired. Otherwise, get the refit track from the desired track
	// map.
	if (fromCocktail)
	  tevTk = muon::tevOptimized(*muon, *trackMapDefault, *trackMapFirstHit,
				     *trackMapPicky);
	else {
	  TrackToTrackMap::const_iterator tevTkRef =
	    trackMap->find(muon->combinedMuon());
	  if (tevTkRef != trackMap->end())
	    tevTk = tevTkRef->val;
	}
	
	// If the TrackRef is valid, make a new MuonTrackLinks that has
	// the same tracker and stand-alone tracks, but has the refit
	// track as its global track.
	if (tevTk.isNonnull())
	  cands->push_back(*cloneAndSwitchTrack(*muon, tevTk));
      }
      else if (fromTrackerTrack)
	cands->push_back(*cloneAndSwitchTrack(*muon, muon->track()));
      else
	cands->push_back(*muon);
    }
  }
  else
    edm::LogWarning("GlobalOnlyMuonProducer")
      << "either " << src << " or the track map(s) " << tevMuonTracks
      << " not present in the event; producing empty collection";
  
  event.put(cands);
}

DEFINE_FWK_MODULE(GlobalOnlyMuonProducer);
