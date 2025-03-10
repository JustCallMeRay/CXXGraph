/***********************************************************/
/***      ______  ____  ______                 _         ***/
/***     / ___\ \/ /\ \/ / ___|_ __ __ _ _ __ | |__	     ***/
/***    | |    \  /  \  / |  _| '__/ _` | '_ \| '_ \	 ***/
/***    | |___ /  \  /  \ |_| | | | (_| | |_) | | | |    ***/
/***     \____/_/\_\/_/\_\____|_|  \__,_| .__/|_| |_|    ***/
/***                                    |_|			     ***/
/***********************************************************/
/***     Header-Only C++ Library for Graph			     ***/
/***	 Representation and Algorithms				     ***/
/***********************************************************/
/***     Author: ZigRazor ***/
/***	 E-Mail: zigrazor@gmail.com 				     ***/
/***********************************************************/
/***	 Collaboration: ----------- 				     ***/
/***********************************************************/
/***	 License: MPL v2.0 ***/
/***********************************************************/

#ifndef __CXXGRAPH_PARTITIONING_HDRF_H__
#define __CXXGRAPH_PARTITIONING_HDRF_H__

#include <memory>
#pragma once

#include <chrono>
#include <random>

#include "CXXGraph/Edge/Edge.h"
#include "CXXGraph/Partitioning/Utility/Globals.hpp"
#include "PartitionStrategy.hpp"

namespace CXXGraph {
// Smart pointers alias
template <typename T>
using unique = std::unique_ptr<T>;
template <typename T>
using shared = std::shared_ptr<T>;

using std::make_shared;
using std::make_unique;

namespace Partitioning {
/**
 * @brief A Vertex Cut Partioning Algorithm ( as described by this paper
 * https://www.fabiopetroni.com/Download/petroni2015HDRF.pdf )
 * @details This algorithm is a greedy algorithm that partitions the graph into
 * n sets of vertices ( as described by this paper
 * https://www.fabiopetroni.com/Download/petroni2015HDRF.pdf ).
 */
template <typename T>
class HDRF : public PartitionStrategy<T> {
 private:
  Globals GLOBALS;

 public:
  explicit HDRF(const Globals &G);
  ~HDRF();

  void performStep(shared<const Edge<T>> e,
                   shared<PartitionState<T>> Sstate) override;
};
template <typename T>
HDRF<T>::HDRF(const Globals &G) : GLOBALS(G) {
  // this->GLOBALS = G;
}
template <typename T>
HDRF<T>::~HDRF() {}
template <typename T>
void HDRF<T>::performStep(shared<const Edge<T>> e,
                          shared<PartitionState<T>> state) {
  int P = GLOBALS.numberOfPartition;
  double lambda = GLOBALS.param1;
  double epsilon = GLOBALS.param2;
  auto nodePair = e->getNodePair();
  CXXGraph::id_t u = nodePair.first->getId();
  CXXGraph::id_t v = nodePair.second->getId();
  std::shared_ptr<Record<T>> u_record = state->getRecord(u);
  std::shared_ptr<Record<T>> v_record = state->getRecord(v);

  //*** ASK FOR LOCK
  bool locks_taken = false;
  while (!locks_taken) {
    srand((unsigned)time(NULL));
    int usleep_time = 2;
    while (!u_record->getLock()) {
      std::this_thread::sleep_for(std::chrono::microseconds(usleep_time));
      usleep_time = (int)pow(usleep_time, 2);
    }
    usleep_time = 2;
    if (u != v) {
      while (!v_record->getLock()) {
        std::this_thread::sleep_for(std::chrono::microseconds(usleep_time));
        usleep_time = (int)pow(usleep_time, 2);

        if (usleep_time > GLOBALS.SLEEP_LIMIT) {
          u_record->releaseLock();
          performStep(e, state);
          return;
        }  // TO AVOID DEADLOCK
      }
    }
    locks_taken = true;
  }
  //*** LOCK TAKEN
  int machine_id = -1;

  //*** COMPUTE MAX AND MIN LOAD
  int MIN_LOAD = state->getMinLoad();
  int MAX_LOAD = state->getMaxLoad();

  //*** COMPUTE SCORES, FIND MIN SCORE, AND COMPUTE CANDIDATES PARITIONS
  std::vector<int> candidates;
  double MAX_SCORE = 0.0;
  for (int m = 0; m < P; m++) {
    int degree_u = u_record->getDegree() + 1;
    int degree_v = v_record->getDegree() + 1;
    int SUM = degree_u + degree_v;
    double fu = 0;
    double fv = 0;
    if (u_record->hasReplicaInPartition(m)) {
      fu = degree_u;
      fu /= SUM;
      fu = 1 + (1 - fu);
    }
    if (v_record->hasReplicaInPartition(m)) {
      fv = degree_v;
      fv /= SUM;
      fv = 1 + (1 - fv);
    }
    int load = state->getMachineLoad(m);
    double bal = (MAX_LOAD - load);
    bal /= (epsilon + MAX_LOAD - MIN_LOAD);
    if (bal < 0) {
      bal = 0;
    }
    double SCORE_m = fu + fv + lambda * bal;
    if (SCORE_m < 0) {
      std::cout << "ERRORE: SCORE_m<0" << std::endl;
      std::cout << "fu: " << fu << std::endl;
      std::cout << "fv: " << fv << std::endl;
      std::cout << "lambda: " << lambda << std::endl;
      std::cout << "bal: " << bal << std::endl;
      exit(-1);
    }
    if (SCORE_m > MAX_SCORE) {
      MAX_SCORE = SCORE_m;
      candidates.clear();
      candidates.push_back(m);
    } else if (SCORE_m == MAX_SCORE) {
      candidates.push_back(m);
    }
  }
  //*** CHECK TO AVOID ERRORS
  if (candidates.empty()) {
    std::cout
        << "ERROR: GreedyObjectiveFunction.performStep -> candidates.isEmpty()"
        << std::endl;
    std::cout << "MAX_SCORE: " << MAX_SCORE << std::endl;
    exit(-1);
  }

  //*** PICK A RANDOM ELEMENT FROM CANDIDATES
  thread_local static std::default_random_engine rand;
  thread_local static std::uniform_int_distribution distribution(0, RAND_MAX);

  unsigned int seed = (unsigned int)time(NULL);
  rand.seed(seed);

  int choice = distribution(rand) % candidates.size();
  machine_id = candidates.at(choice);
  try {
    shared<CoordinatedPartitionState<T>> cord_state =
        std::static_pointer_cast<CoordinatedPartitionState<T>>(state);
    // NEW UPDATE RECORDS RULE TO UPFDATE THE SIZE OF THE PARTITIONS EXPRESSED
    // AS THE NUMBER OF VERTICES THEY CONTAINS
    if (!u_record->hasReplicaInPartition(machine_id)) {
      u_record->addPartition(machine_id);
      cord_state->incrementMachineLoadVertices(machine_id);
    }
    if (!v_record->hasReplicaInPartition(machine_id)) {
      v_record->addPartition(machine_id);
      cord_state->incrementMachineLoadVertices(machine_id);
    }
  } catch (const std::bad_cast &) {
    // use employee's member functions
    // 1-UPDATE RECORDS
    if (!u_record->hasReplicaInPartition(machine_id)) {
      u_record->addPartition(machine_id);
    }
    if (!v_record->hasReplicaInPartition(machine_id)) {
      v_record->addPartition(machine_id);
    }
  }

  // 2-UPDATE EDGES
  state->incrementMachineLoad(machine_id, e);

  // 3-UPDATE DEGREES
  u_record->incrementDegree();
  v_record->incrementDegree();

  //*** RELEASE LOCK
  u_record->releaseLock();
  v_record->releaseLock();
  return;
}
}  // namespace Partitioning
}  // namespace CXXGraph

#endif  // __CXXGRAPH_PARTITIONING_HDRF_H__
