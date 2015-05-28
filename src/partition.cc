#include "partition.h"

#include <random>
#include <sstream>
#include <errno.h>
#include <chrono>
#include <unordered_map>
#include <algorithm>

using namespace khmer;
using namespace khmer::read_parsers;
using namespace std;

size_t Partition::output_sampled_partitions(
    const string &left,
    const string &right,
    const string &out_left,
    const string &out_right,
    double rate,
    int usrseed,
    int k,
    bool diginorm) {

  IParser* left_parser = IParser::get_parser(left);
  IParser* right_parser = IParser::get_parser(right);
  ofstream left_outstream(out_left.c_str());
  ofstream right_outstream(out_right.c_str());

  PartitionSet partitions;

  //
  // iterate through all partitions and sample them probabilistically
  //
  unsigned seed;
  if (usrseed > -1) {
    seed = usrseed;
  } else {
    seed = std::chrono::system_clock::now().time_since_epoch().count();
  }
  default_random_engine generator(seed);
  uniform_real_distribution<double> distribution(0.0, 1.0);

  unordered_map <PartitionID, CountingHash> countinghash_map;
  HashIntoType hashsize = 1e5+3;

  for(ReversePartitionMap::iterator it = reverse_pmap.begin();
      it != reverse_pmap.end(); ++it) {

    double p = distribution(generator);

    if (p > rate) {
      // not in the sample
      continue;
    }

    PartitionID pid = it->first;
    partitions.insert(pid);

    // create counting hash for this partition
    countinghash_map.emplace(std::piecewise_construct, std::forward_as_tuple(pid), std::forward_as_tuple(k, hashsize));

    cout << "including partition " << it->first << " in sample" << endl;
  }

  Read read_left;
  Read read_right;
  string seq_left;
  string seq_right;

  HashIntoType kmer_left = 0;
  HashIntoType kmer_right = 0;

  const unsigned int ksize = _ht->ksize();

  //
  // go through all the reads, take those with assigned partitions
  // and output them only if their partition was in the subsample
  //
  while(!left_parser->is_complete()) {
    if (right_parser->is_complete()) {
      cerr << "ERROR: read files have different numbers of reads" << endl;
      exit(1);
    }
    read_left = left_parser->get_next_read();
    read_right = right_parser->get_next_read();
    seq_left = read_left.sequence;
    seq_right = read_right.sequence;

    if (_ht->check_and_normalize_read(seq_left) &&
        _ht->check_and_normalize_read(seq_right)) {

      const char * kmer_s_left = seq_left.c_str();
      const char * kmer_s_right = seq_right.c_str();

      bool found_tags = false;

      for (unsigned int i = 0; i < seq_left.length() - ksize + 1; i++) {
        kmer_left = _hash(kmer_s_left + i, ksize);

        // are these both known tags
        if (set_contains(partition_map, kmer_left)) {
          found_tags = true;
        }
      }

      for (unsigned int i = 0; i < seq_right.length() - ksize + 1; i++) {
        kmer_right = _hash(kmer_s_right + i, ksize);

        // are these both known tags
        if (set_contains(partition_map, kmer_right)) {
          found_tags = true;
          break;
        }
      }

      PartitionID partition_left;
      PartitionID partition_right;
      if (found_tags) {
        PartitionID* partition_ptr_left;
        PartitionID* partition_ptr_right;

        partition_ptr_left = partition_map[kmer_left];
        partition_ptr_right = partition_map[kmer_right];
        if (partition_ptr_left == NULL || partition_ptr_right == NULL) {
          continue;
        }

        partition_left = *partition_ptr_left;
        partition_right = *partition_ptr_right;
      }

      // only write out if partition is in the sample
      bool leftfound = partitions.find(partition_left) != partitions.end();
      bool rightfound = partitions.find(partition_right) != partitions.end();

      if (leftfound || rightfound) {

        if (diginorm) {
          bool left_pass = true;
          bool right_pass = true;

          // left
          auto chl_find = countinghash_map.find(partition_left);
          if (chl_find != countinghash_map.end()) {
            left_pass = pass_coverage_filter(read_left, chl_find->second, k);
          }

          // right
          auto chr_find = countinghash_map.find(partition_right);
          if (chr_find != countinghash_map.end()) {
            right_pass = pass_coverage_filter(read_right, chr_find->second, k);
          }

          if (!(left_pass && right_pass)) {
            continue;
          }
        }

        if (read_left.quality.length()) { // FASTQ

          left_outstream << "@" << read_left.name << "\t" << partition_left;
          left_outstream << endl << seq_left << endl << '+' << endl;
          left_outstream << read_left.quality << endl;

          right_outstream << "@" << read_right.name << "\t" << partition_right;
          right_outstream << endl << seq_right << endl << '+' << endl;
          right_outstream << read_right.quality << endl;

        } else {  // FASTA

          left_outstream << ">" << read_left.name << "\t" << partition_left;
          left_outstream << endl << seq_left << endl;

          right_outstream << ">" << read_right.name << "\t" << partition_right;
          right_outstream << endl << seq_right << endl;

        }
      }

    }
  }

  delete left_parser;
  left_parser = NULL;
  delete right_parser;
  right_parser = NULL;

  return partitions.size();
}

bool Partition::pass_coverage_filter(Read& read, CountingHash& hash, int k) {

  BoundedCounterType mincov = 100;
  const char * kmer_s = read.sequence.c_str();

  for (unsigned int i = 0; i < read.sequence.length() - k + 1; i++) {

    BoundedCounterType count = hash.get_count(kmer_s);
    mincov = min(count, mincov);

  }

  return mincov < 20;
}
