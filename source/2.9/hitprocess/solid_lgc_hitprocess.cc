// %%%%%%%%%%
// G4 headers
// %%%%%%%%%%
#include "G4UnitsTable.hh"
#include "G4MaterialPropertyVector.hh"
#include "Randomize.hh"
#include <CCDB/Calibration.h>
#include <CCDB/Model/Assignment.h>
#include <CCDB/CalibrationGenerator.h>
using namespace ccdb;

// %%%%%%%%%%%%%
// gemc headers
// %%%%%%%%%%%%%
#include "solid_lgc_hitprocess.h"
#include "detector.h"
#include <set>

// CLHEP units
#include "CLHEP/Units/PhysicalConstants.h"
using namespace CLHEP;


//Here we are going to define a single pixel on the PMT face.  We will let the pixel size and placement be controlled by the geometry.  Each pixel will have a local x-y placement, a PMT x-y placement and a sector identification.  Otherwise, this is treated much like the htcc hitprocess from clas12

map<string, double> solid_lgc_HitProcess :: integrateDgt(MHit* aHit, int hitn)
{
  map<string, double> dgtz;
  vector<identifier> identity = aHit->GetId();
	
  // if the particle is not an opticalphoton return empty bank
//   if(aHit->GetPID() != 0) return dgtz;
  if(aHit->GetPID() != -22) return dgtz;  //optical photon pid changed from 0 to -22 in geant4.10.7
  trueInfos tInfos(aHit);
	
  // Since the hit involves a PMT which detects photons with a certain quantum efficiency (QE)
  // we want to implement QE here in a flexible way:
	
  // That means we want to find out if the material of the photocathode has been defined,
  // and if so, find out if it has a material properties table which defines an efficiency.
  // if we find both of these properties, then we accept this event with probability QE:
	
  vector<int> tids = aHit->GetTIds(); // track ID at EACH STEP
  vector<int> pids = aHit->GetPIDs(); // particle ID at EACH STEP
  set<int> TIDS;                      // an array containing all UNIQUE tracks in this hit
  vector<double> photon_energies;
	
  vector<double> Energies = aHit->GetEs();
  unsigned nsteps = tids.size();
	
  for(unsigned int s=0; s<nsteps; s++)
    {
      // selecting optical photons
//       if(pids[s] == 0) 
	if(pids[s] == -22) //optical photon pid changed from 0 to -22 in geant4.10.7
	{
	  // insert this step into the set of track ids (set can only have unique values).
	  pair< set<int> ::iterator, bool> newtrack = TIDS.insert(tids[s]);
	  
	  // if we found a new track, then add the initial photon energy of this
	  // track to the list of photon energies, for when we calculate quantum efficiency later
	  if( newtrack.second ) photon_energies.push_back( Energies[s] );
	}
    }
	
  // we want to crash if identity doesn't have size 3
  int isector = identity[0].id;
  int ipmt   = identity[1].id;
  int ipix   = identity[2].id;

	
  // if identifiers for theta and phi indices have been defined, use them.
  // otherwise, calculate itheta and iphi from the (mandatory) pmt index
  // assuming the default configuration:
  // int isector=-1, ipmtx=-1, ipmty=-1, ipixx=-1, ipixy=-1;
	
	
	// what is going on here i dont think this is right
	// double check!!
  /*
	if( idsector >= 0 && idpmtx >= 0 && idpmty >= 0 && idpixx >= 0 && idpixy >= 0)
	{
	  isector = identity[0].id;
	  ipmtx   = identity[1].id;
	  ipmty   = identity[2].id;
	  ipixx   = identity[3].id;
	  ipixy   = identity[4].id;
	}
	else return dgtz; //no valid ID
  */


	
	// here is the fun part: figure out the number of photons we detect based
	// on the quantum efficiency of the photocathode material, if defined:
	
	int ndetected = 0;
	
	// If the detector corresponding to this hit has a material properties table with "Efficiency" defined:
	G4MaterialPropertiesTable* MPT = aHit->GetDetector().GetLogical()->GetMaterial()->GetMaterialPropertiesTable();
	G4MaterialPropertyVector* efficiency = NULL;
	
	bool gotefficiency = false;
	if( MPT != NULL )
	{
		efficiency = (G4MaterialPropertyVector*) MPT->GetProperty("EFFICIENCY");
		if( efficiency != NULL ) gotefficiency = true;
	}
	//cout << aHit->GetDetector().GetLogical()->GetMaterial()->GetName() << "  " << gotefficiency << endl;
	for( unsigned int iphoton = 0; iphoton<TIDS.size(); iphoton++ )
	{
		//loop over all unique photons contributing to the hit:
		if( gotefficiency )
		{
			//If the material of this detector has a material properties table
			// with "EFFICIENCY" defined, then "detect" this photon with probability = efficiency
			bool outofrange = false;
			if( G4UniformRand() <= efficiency->GetValue( photon_energies[iphoton], outofrange ) )
				ndetected++;
			
			if( verbosity > 4 )
			{
				cout << log_msg << " Found efficiency definition for material "
				<< aHit->GetDetector().GetLogical()->GetMaterial()->GetName()
				<< ": (Ephoton, efficiency)=(" << photon_energies[iphoton] << ", "
				<< ( (G4MaterialPropertyVector*) efficiency )->GetValue( photon_energies[iphoton], outofrange )
				<< ")" << endl;
			}
		}
		else
		{
			//No efficiency definition, "detect" all photons
			ndetected++;
		}
	}
	/*	
	if(verbosity>4)
	{
		trueInfos tInfos(aHit);

	
		cout <<  log_msg << " (sector, pmt-x, pmt-y, pix-x, pix-y)=(" << isector << ", " << ipmtx << ", " << ipmty <<" , " << ipixx << ", " << ipixy <<")"
		     << " x=" << tInfos.x/cm << " y=" << tInfos.y/cm << " z=" << tInfos.z/cm << endl;
	}
	*/
	dgtz["sector"] = isector;
	dgtz["pmt"]   = ipmt;
	dgtz["pixel"]   = ipix;
	dgtz["nphe"]   = ndetected;
	dgtz["avg_t"]   = tInfos.time;
	dgtz["hitn"]   = hitn;

	
	return dgtz;
}

vector<identifier>  solid_lgc_HitProcess :: processID(vector<identifier> id, G4Step* aStep, detector Detector)
{
	id[id.size()-1].id_sharing = 1;
	return id;
}

// - electronicNoise: returns a vector of hits generated / by electronics.
vector<MHit*> solid_lgc_HitProcess :: electronicNoise()
{
	vector<MHit*> noiseHits;

	// first, identify the cells that would have electronic noise
	// then instantiate hit with energy E, time T, identifier IDF:
	//
	// MHit* thisNoiseHit = new MHit(E, T, IDF, pid);

	// push to noiseHits collection:
	// noiseHits.push_back(thisNoiseHit)

	return noiseHits;
}


map< string, vector <int> >  solid_lgc_HitProcess :: multiDgt(MHit* aHit, int hitn)
{
	map< string, vector <int> > MH;
	return MH;
}


// - charge: returns charge/time digitized information / step
map< int, vector <double> > solid_lgc_HitProcess :: chargeTime(MHit* aHit, int hitn)
{
	map< int, vector <double> >  CT;

	return CT;
}

// - voltage: returns a voltage value for a given time. The inputs are:
// charge value (coming from chargeAtElectronics)
// time (coming from timeAtElectronics)
double solid_lgc_HitProcess :: voltage(double charge, double time, double forTime)
{
	return 0.0;
}

