#include <random>
#include <string>
#include <sstream>
#include <errno.h>

#include "hashbits.hh"
#include "subset.hh"
#include "read_parsers.hh"

namespace khmer {

class Part : public SubsetPartition {
public:
  Part(Hashtable * ht);
  size_t output_sampled_partitions(const std::string &infilename,
                            const std::string &outputfile);
};

}

// #define VALIDATE_PARTITIONS

using namespace khmer;
using namespace khmer:: read_parsers;
using namespace std;
Part::Part(Hashtable * ht) : SubsetPartition(ht) {

}

size_t Part::output_sampled_partitions(
    const std::string	&infilename,
    const std::string	&outputfile) {

  IParser* parser = IParser::get_parser(infilename);
  ofstream outfile(outputfile.c_str());

  unsigned int min_kmerset_size = 3;

  PartitionSet partitions;

  //
  // iterate through all partitions and sample them probabilistically
  //
  std::default_random_engine generator;
  std::uniform_real_distribution<double> distribution(0.0, 1.0);
  double cutoff = 0.2;

  for(ReversePartitionMap::iterator it = reverse_pmap.begin();
      it != reverse_pmap.end(); ++it) {
    double p = distribution(generator);

    if (p > cutoff) {
      // not in the sample
      continue;
    }

    PartitionPtrSet *kmerset = it->second;

    if (kmerset->size() >= min_kmerset_size) {
      partitions.insert(it->first);
      cout << "including partition " << it->first << " in sample\n";
    }
  }

  for(auto p : partitions) {
    cout << "partition " << p << " is in sample" << endl;
  }

  Read read;
  string seq;

  HashIntoType kmer = 0;

  const unsigned int ksize = _ht->ksize();

  //
  // go through all the reads, take those with assigned partitions
  // and output them only if their partition was in the subsample
  //
  while(!parser->is_complete()) {
    read = parser->get_next_read();
    seq = read.sequence;

    if (_ht->check_and_normalize_read(seq)) {
      const char * kmer_s = seq.c_str();

      bool found_tag = false;
      for (unsigned int i = 0; i < seq.length() - ksize + 1; i++) {
        kmer = _hash(kmer_s + i, ksize);

        // is this a known tag?
        if (set_contains(partition_map, kmer)) {
          found_tag = true;
          break;
        }
      }

      PartitionID partition_id = 0;
      if (found_tag) {
        PartitionID * partition_p = partition_map[kmer];
        if (partition_p == NULL) {
          partition_id = 0;
        } else {
          partition_id = *partition_p;
        }
      }

      if (partition_id == 0) {
        continue;
      }

      // only write out if partition is in the sample
      PartitionSet::iterator it = partitions.find(partition_id);
      if (it != partitions.end()) {
        // cout << "read in sampled partition " << partition_id << " (it = " << *it << ")" << endl;
        if (read.quality.length()) { // FASTQ
          outfile << "@" << read.name << "\t" << partition_id
                  << "\n";
          outfile << seq << "\n+\n";
          outfile << read.quality << "\n";
        } else {  // FASTA
          outfile << ">" << read.name << "\t" << partition_id;
          outfile << "\n" << seq << "\n";
        }
      }

    }
  }

  delete parser;
  parser = NULL;

  return partitions.size();
}
