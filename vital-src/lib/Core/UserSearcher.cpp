//===-- UserSearcher.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "UserSearcher.h"

#include "Executor.h"
#include "MergeHandler.h"
#include "Searcher.h"

#include "klee/Support/ErrorHandling.h"

#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace klee;

extern bool is_simulation_mode;
extern std::set<std::string> globalRecordingSet;
#include <fstream>
#include <sstream>

namespace {
llvm::cl::OptionCategory
    SearchCat("Search options", "These options control the search heuristic.");

cl::list<Searcher::CoreSearchType> CoreSearch(
    "search",
    cl::desc("Specify the search heuristic (default=random-path interleaved "
             "with nurs:covnew)"),
    cl::values(
        clEnumValN(Searcher::DFS, "dfs", "use Depth First Search (DFS)"),
        clEnumValN(Searcher::BFS, "bfs",
                   "use Breadth First Search (BFS), where scheduling decisions "
                   "are taken at the level of (2-way) forks"),
        clEnumValN(Searcher::RandomState, "random-state",
                   "randomly select a state to explore"),
        clEnumValN(Searcher::RandomPath, "random-path",
                   "use Random Path Selection (see OSDI'08 paper)"),
        clEnumValN(Searcher::NURS_CovNew, "nurs:covnew",
                   "use Non Uniform Random Search (NURS) with Coverage-New"),
        clEnumValN(Searcher::NURS_MD2U, "nurs:md2u",
                   "use NURS with Min-Dist-to-Uncovered"),
        clEnumValN(Searcher::NURS_Depth, "nurs:depth", "use NURS with depth"),
        clEnumValN(Searcher::NURS_RP, "nurs:rp", "use NURS with 1/2^depth"),
        clEnumValN(Searcher::NURS_ICnt, "nurs:icnt",
                   "use NURS with Instr-Count"),
        clEnumValN(Searcher::NURS_CPICnt, "nurs:cpicnt",
                   "use NURS with CallPath-Instr-Count"),
        clEnumValN(Searcher::NURS_QC, "nurs:qc", "use NURS with Query-Cost"),
        clEnumValN(Searcher::MCTS, "mcts", "use Monte Carlo Tree Search (MCTS) algorithm")), // THX
    cl::cat(SearchCat));

cl::opt<bool> UseIterativeDeepeningTimeSearch(
    "use-iterative-deepening-time-search",
    cl::desc(
        "Use iterative deepening time search (experimental) (default=false)"),
    cl::init(false),
    cl::cat(SearchCat));

cl::opt<bool> UseBatchingSearch(
    "use-batching-search",
    cl::desc("Use batching searcher (keep running selected state for N "
             "instructions/time, see --batch-instructions and --batch-time) "
             "(default=false)"),
    cl::init(false),
    cl::cat(SearchCat));

cl::opt<unsigned> BatchInstructions(
    "batch-instructions",
    cl::desc("Number of instructions to batch when using "
             "--use-batching-search.  Set to 0 to disable (default=10000)"),
    cl::init(10000),
    cl::cat(SearchCat));

cl::opt<std::string> BatchTime(
    "batch-time",
    cl::desc("Amount of time to batch when using "
             "--use-batching-search.  Set to 0s to disable (default=5s)"),
    cl::init("5s"),
    cl::cat(SearchCat));

cl::opt<std::string> PackageName(
    "package-name",
    cl::desc("Package name under test "
             "(the name is bitcode file name without .bc, e.g., file test.bc should input --package-name=test)."),
    //cl::init("5s"),
    cl::cat(SearchCat));

cl::list<Searcher::SimulationSearchType> SimulationSearch(
    "simulation-search",
    cl::desc("Specify the simulation search heuristic (DFS search by default)"),
	  cl::values(
      clEnumValN(Searcher::SIM_DFS, "dfs", "use depth first search"),
      clEnumValN(Searcher::SIM_RandomPath, "random-path", "use random path selection")),
      cl::cat(SearchCat)
  );

cl::opt<bool> enableSimulation(
    "enable-simulation",
    cl::desc("Specify if turn on the simulation during MCTS path exploration "
             "used with -simulation-search) "
             "(default=true)"),
    cl::init(true),
    cl::cat(SearchCat));

cl::opt<unsigned int> simulationLimit(
    "optimiation-degree",
    cl::desc("set a degree of simulation optimiztion (default=700)"),
    cl::init(700),
    cl::cat(SearchCat));

cl::opt<bool> enableSimulationOpt(
    "enable-simulation-opt",
    cl::desc("Specify if turn on the simulation optimization during MCTS path exploration "
             "(default=true)"),
    cl::init(true),
    cl::cat(SearchCat));
} // namespace

void klee::initializeSearchOptions() {
  // default values
  if (CoreSearch.empty()) {
    if (UseMerge){
      CoreSearch.push_back(Searcher::NURS_CovNew);
      klee_warning("--use-merge enabled. Using NURS_CovNew as default searcher.");
    } else {
      CoreSearch.push_back(Searcher::RandomPath);
      CoreSearch.push_back(Searcher::NURS_CovNew);
    }
  }
}

bool klee::userSearcherRequiresMD2U() {
  return (std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_MD2U) != CoreSearch.end() ||
          std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_CovNew) != CoreSearch.end() ||
          std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_ICnt) != CoreSearch.end() ||
          std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_CPICnt) != CoreSearch.end() ||
          std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_QC) != CoreSearch.end());
}

Searcher *getNewSearcher(Searcher::CoreSearchType type, RNG &rng, PTree &processTree, Executor &executor) { // THX add executor parameter
  Searcher *searcher = nullptr;
  switch (type) {
    case Searcher::DFS: searcher = new DFSSearcher(); break;
    case Searcher::BFS: searcher = new BFSSearcher(); break;
    case Searcher::RandomState: searcher = new RandomSearcher(rng); break;
    case Searcher::RandomPath: searcher = new RandomPathSearcher(processTree, rng); break;
    case Searcher::MCTS: searcher = new MCTSSearcher(processTree, executor, rng); break; // THX for MCTS path search
    case Searcher::NURS_CovNew: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::CoveringNew, rng); break;
    case Searcher::NURS_MD2U: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::MinDistToUncovered, rng); break;
    case Searcher::NURS_Depth: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::Depth, rng); break;
    case Searcher::NURS_RP: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::RP, rng); break;
    case Searcher::NURS_ICnt: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::InstCount, rng); break;
    case Searcher::NURS_CPICnt: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::CPInstCount, rng); break;
    case Searcher::NURS_QC: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::QueryCost, rng); break;
  }

  // Assign enable_simulation here
  enable_simulation = enableSimulation;
  simulation_limit = simulationLimit;
  simulation_opt = enableSimulationOpt;
  return searcher;
}

Searcher *klee::constructUserSearcher(Executor &executor) {

  // load the type inference results here
    llvm::errs() << "Now test " << PackageName << "\n";
        std::ifstream file_name;
        std::string temp;
        std::string filename = "xxxx" + PackageName + ".txt"; // TODO dynamically load the file
        file_name.open(filename);
        if (file_name.fail()){
            llvm::errs() << "Error when opening recordings.txt file " << filename << "!\n";
            exit(1);
        }
        while (std::getline(file_name, temp)){
            globalRecordingSet.insert(temp);
        }
        file_name.close();

  Searcher *searcher = getNewSearcher(CoreSearch[0], executor.theRNG, *executor.processTree, executor); //THX added executor for mcts
  llvm::raw_ostream &os = executor.getHandler().getInfoStream();
  llvm::errs() << "***** Emmmm.... This is in the normal running mode!!!\n";
  if (CoreSearch.size() > 1) {
    std::vector<Searcher *> s;
    s.push_back(searcher);

    for (unsigned i = 1; i < CoreSearch.size(); i++)
      s.push_back(getNewSearcher(CoreSearch[i], executor.theRNG, *executor.processTree, executor)); // THX added the executor for mcts

    searcher = new InterleavedSearcher(s);
  }

  if (UseBatchingSearch) {
    searcher = new BatchingSearcher(searcher, time::Span(BatchTime),
                                    BatchInstructions);
  }

  if (UseIterativeDeepeningTimeSearch) {
    searcher = new IterativeDeepeningTimeSearcher(searcher);
  }

  if (UseMerge) {
    auto *ms = new MergingSearcher(searcher);
    executor.setMergingSearcher(ms);

    searcher = ms;
  }

    /* TODO: Should both of the searchers be of the same type? */
  llvm::errs() << "--- Entering SplittedSearcher \n";

  if (!SimulationSearch.empty()) {
    Searcher *simulationSearcher = NULL;
    switch (SimulationSearch[0]) {
    case Searcher::SIM_DFS:
        simulationSearcher = new DFSSearcher();
        break;
    case Searcher::SIM_RandomPath:
        simulationSearcher = new RandomPathSearcher(*executor.processTree, executor.theRNG);
        break;
    }

    if (simulationSearcher == NULL) {
      klee_error("invalid simulation search heuristic");
    }
    searcher = new SplittedSearcher(searcher, simulationSearcher, 1);

  } 

  os << "BEGIN searcher description\n";
  searcher->printName(os);
  os << "END searcher description\n";

  return searcher;
}
