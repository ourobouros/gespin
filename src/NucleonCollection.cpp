#include "NucleonCollection.h"
#include "Nucleon.h"

#include "math.h"

NucleonCollection::NucleonCollection(double pairwise_max, unsigned int units, double length)
  : likelihood(1), nucleon_count(0), units(units), length(length), cube_length(length/units) {
    pairwise_units = ceil(pairwise_max/cube_length);
    cubes = new nucleon_array** [2*units];
    for(unsigned int i = 0; i < 2*units; i++) {
        cubes[i] = new nucleon_array* [2*units];
        for(unsigned int j = 0; j < 2*units; j++) {
            cubes[i][j] = new nucleon_array [2*units];
        }
    }

    //the default likelihood will be 1 regardless of the configuration
    single_likelihood = [](Nucleon&) { return 1; };
    pairwise_likelihood = [](Nucleon&, Nucleon&) { return 1; };

    recording = false;
}

NucleonCollection::NucleonCollection(const NucleonCollection &nucleon_collection) {
    nucleon_count = 0;
    units = nucleon_collection.units;
    length = nucleon_collection.length;
    cube_length = nucleon_collection.cube_length;

    for(unsigned int i = 0; i < nucleon_collection.NucleonCount(); i++) {
        AddNucleon(nucleon_collection[i]);
    }
}

NucleonCollection::~NucleonCollection() {
    //this deletes all of the nucleons
    Reset();

    for(unsigned int i = 0; i < 2*units; i++) {
        for(unsigned int j = 0; j < 2*units; j++) {
            delete [] cubes[i][j];
        }
        delete [] cubes[i];
    }
    delete cubes;
}

unsigned int NucleonCollection::AddNucleon(const Nucleon& nucleon) {
    return InsertNucleon(nucleon, ordered.end());
}

unsigned int NucleonCollection::InsertNucleon(const Nucleon& nucleon, NucleonCollection::nucleon_array::iterator insert_point) {
    Nucleon *new_nucleon = new Nucleon(nucleon);
    return InsertExistingNucleon(*new_nucleon, insert_point);
}

unsigned int NucleonCollection::InsertExistingNucleon(Nucleon& nucleon, NucleonCollection::nucleon_array::iterator insert_point) {
    BringInsideRegion(&nucleon);

    ordered.insert(insert_point, &nucleon);

    nucleon.cube = &FindCube(nucleon.x, nucleon.y, nucleon.z, \
                                  nucleon.cube_i, nucleon.cube_j, nucleon.cube_k);
    nucleon.cube->push_back(&nucleon);
    nucleon.parent = this;

    nucleon.single_likelihood = SingleLikelihood(nucleon);
    likelihood *= nucleon.single_likelihood;
    nucleon.pairwise_likelihoods.clear();
    //in this case the pairwise max is 0 so we only consider single body
    if(pairwise_units == 0) {
      return ++nucleon_count;
    }

    //the key here is that the nucleon is shifted to satisfy the continuous boundary conditions
    for(int i = nucleon.cube_i - pairwise_units; i <= nucleon.cube_i + pairwise_units; i++) {
        double x_offset = -2.0*length*double( (i/(2*units)) - (i<0?1:0));
        nucleon.x += x_offset;
        for(int j = nucleon.cube_j - pairwise_units; j <= nucleon.cube_j + pairwise_units; j++) {
            double y_offset = -2.0*length*double( (j/(2*units)) - (j<0?1:0));
            nucleon.y += y_offset;
            for(int k = nucleon.cube_k - pairwise_units; k <= nucleon.cube_k + pairwise_units; k++) {
                double z_offset = -2.0*length*double( (k/(2*units)) - (k<0?1:0));
                nucleon.z += z_offset;
                nucleon_array &cube = cubes[i%(units*2)][j%(units*2)][k%(units*2)];
                for(nucleon_array::iterator it = cube.begin(); it != cube.end(); it++) {
                    if(*it != &nucleon) {
                        const double pair_likelihood = PairwiseLikelihood(**it, nucleon);
                        likelihood *= pair_likelihood;
                        nucleon.pairwise_likelihoods.push_back(std::make_pair(*it, pair_likelihood));
                        (*it)->pairwise_likelihoods.push_back(std::make_pair(&nucleon, pair_likelihood));
                    }
                }
                nucleon.z -= z_offset;
            }
            nucleon.y -= y_offset;
        }
        nucleon.x -= x_offset;
    }

    return ++nucleon_count;
}

void NucleonCollection::UpdateLikelihood() {
    likelihood = 1.0;
    for(nucleon_array::iterator it1 = ordered.begin(); it1 != ordered.end(); it1++) {
        (*it1)->single_likelihood = SingleLikelihood(**it1);
        likelihood *= pow((*it1)->single_likelihood, 2);

        std::vector< std::pair<Nucleon*, double> >::iterator it2;
        for(it2 = (*it1)->pairwise_likelihoods.begin(); it2 != (*it1)->pairwise_likelihoods.end(); it2++) {
            it2->second = PairwiseLikelihood(**it1, *(it2->first));
            likelihood *= it2->second;
        }
    }
    //this impletmentation double counts everything so we need to take the square root
    likelihood = sqrt(likelihood);
}

NucleonCollection::nucleon_array& NucleonCollection::FindCube(double x, double y, double z, int &i, int &j, int &k) const {
    i = floor(x/cube_length);
    j = floor(y/cube_length);
    k = floor(z/cube_length);

    i = (i + units)%(2*units);
    j = (j + units)%(2*units);
    k = (k + units)%(2*units);

    return cubes[i][j][k];
}

NucleonCollection::nucleon_array& NucleonCollection::FindCube(double x, double y, double z) const {
    int i, j, k;
    return FindCube(x, y, z, i, j, k);
}

NucleonCollection::nucleon_array& NucleonCollection::FindCube(const Nucleon &nucleon) const {
    return FindCube(nucleon.X(), nucleon.Y(), nucleon.Z());
}

void NucleonCollection::Reset(bool delete_nucleons) {
    for(nucleon_array::iterator it = ordered.begin(); it != ordered.end(); it++) {
        (*it)->cube->clear();
        if(delete_nucleons) {
            delete *it;
        }
    }
    ordered.clear();
    nucleon_count = 0;
}

void NucleonCollection::BringInsideRegion(Nucleon *nucleon) {
    while(nucleon->x > length) nucleon->x -= 2.0*length;
    while(nucleon->x < -length) nucleon->x += 2.0*length;
    while(nucleon->y > length) nucleon->y -= 2.0*length;
    while(nucleon->y < -length) nucleon->y += 2.0*length;
    while(nucleon->z > length) nucleon->z -= 2.0*length;
    while(nucleon->z < -length) nucleon->z += 2.0*length;
}

double NucleonCollection::Checkpoint() {
    cached_likelihoods.clear();
    cached_nucleons.clear();
    cached_x.clear();
    cached_y.clear();
    cached_z.clear();
    recording = true;
    return likelihood;
}

double NucleonCollection::Revert() {
    if(cached_nucleons.size() == 0) {
        return likelihood;
    }

    recording = false;
    Nucleon *last_nucleon = 0;
    double last_x, last_y, last_z, last_likelihood;
    while(cached_nucleons.size() > 0) {
        Nucleon* next_nucleon = cached_nucleons.back(); cached_nucleons.pop_back();
        if(next_nucleon != last_nucleon && last_nucleon != 0) {
            SetNucleonPosition(last_nucleon, last_x, last_y, last_z);
        }
        last_nucleon = next_nucleon;
        last_likelihood = cached_likelihoods.back(); 
        last_x = cached_x.back();
        last_y = cached_y.back();
        last_z = cached_z.back();
        cached_likelihoods.pop_back();
        cached_x.pop_back();
        cached_y.pop_back();
        cached_z.pop_back();
    }
    SetNucleonPosition(last_nucleon, last_x, last_y, last_z);
    likelihood = last_likelihood;
    recording = true;
    return likelihood;
}

void NucleonCollection::CacheState(Nucleon *nucleon) {
    cached_likelihoods.push_back(likelihood);
    cached_x.push_back(nucleon->x); 
    cached_y.push_back(nucleon->y); 
    cached_z.push_back(nucleon->z); 
    cached_nucleons.push_back(nucleon);
}

void NucleonCollection::SetNucleonPosition(Nucleon *nucleon, double x, double y, double z) {
    if(recording) {
        CacheState(nucleon);
    }

    nucleon_array& new_cube = FindCube(x, y, z, nucleon->cube_i, nucleon->cube_j, nucleon->cube_k);
    nucleon->x = x;
    nucleon->y = y;
    nucleon->z = z;
    if(&new_cube == nucleon->cube) {
        likelihood /= nucleon->single_likelihood;
        nucleon->single_likelihood = SingleLikelihood(*nucleon);
        likelihood *= nucleon->single_likelihood;

        std::vector< std::pair<Nucleon*, double> >::iterator it1;
        for(it1 = nucleon->pairwise_likelihoods.begin(); it1 != nucleon->pairwise_likelihoods.end(); it1++) {
            Nucleon *nucleon2 = it1->first;
            likelihood /= it1->second;
            it1->second = PairwiseLikelihood(*nucleon, *nucleon2);
            likelihood *= it1->second;

            std::vector< std::pair<Nucleon*, double> >::iterator it2;
            for(it2 = nucleon2->pairwise_likelihoods.begin(); it2 != nucleon2->pairwise_likelihoods.end(); it2++) {
                if(nucleon == (*it2).first) {
                    it2->second = it1->second;
                    nucleon2->pairwise_likelihoods.erase(it2);
                    break;
                }
            }
        }
    }
    else {
        NucleonCollection::nucleon_array::iterator insert_point = RemoveNucleon(nucleon);
        InsertExistingNucleon(*nucleon, insert_point);
    }
}

NucleonCollection::nucleon_array::iterator NucleonCollection::RemoveNucleonFromCube(Nucleon *nucleon) {
    for(nucleon_array::iterator it = nucleon->cube->begin(); it != nucleon->cube->end(); it++) {
        if(*it == nucleon) {
            return nucleon->cube->erase(it);
        }
    }
    return nucleon->cube->end();
}

NucleonCollection::nucleon_array::iterator NucleonCollection::RemoveNucleonFromOrdered(Nucleon *nucleon) {
    for(nucleon_array::iterator it = ordered.begin(); it != ordered.end(); it++) {
        if(*it == nucleon) {
            nucleon_count--;
            return ordered.erase(it);
        }
    }
    return ordered.end();
}

NucleonCollection::nucleon_array::iterator NucleonCollection::RemoveNucleon(Nucleon *nucleon) {
    likelihood /= nucleon->single_likelihood;

    std::vector< std::pair<Nucleon*, double> >::iterator it1;
    for(it1 = nucleon->pairwise_likelihoods.begin(); it1 != nucleon->pairwise_likelihoods.end(); it1++) {
        likelihood /= (*it1).second;
        Nucleon *nucleon2 = (*it1).first;
        std::vector< std::pair<Nucleon*, double> >::iterator it2;
        for(it2 = nucleon2->pairwise_likelihoods.begin(); it2 != nucleon2->pairwise_likelihoods.end(); it2++) {
            if(nucleon == (*it2).first) {
                nucleon2->pairwise_likelihoods.erase(it2);
                break;
            }
        }
    }
    nucleon->pairwise_likelihoods.clear();

    RemoveNucleonFromCube(nucleon);
    return RemoveNucleonFromOrdered(nucleon);
}
