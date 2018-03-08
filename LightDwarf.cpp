// Copyright 2017 Adrien Guinet <adrien@guinet.me>

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/Support/raw_ostream.h"


using namespace llvm;

namespace {

// From apple's clang!
class DIFileRecursiveModify {
  SmallVector<MDNode *, 16> Worklist;
  DenseSet<const MDNode *> Visited;

  LLVMContext &Ctx;

public:
  DIFileRecursiveModify(LLVMContext &Context)
      : Ctx(Context) {}

  /// \brief Add a new entry point into the metadata graph to traverse from
  void addEntryPoint(MDNode *N) {
    if (!N || !Visited.count(N))
      Worklist.push_back(N);
  }
  void addEntryPoint(NamedMDNode *NMD) {
    for (auto I : NMD->operands())
      addEntryPoint(I);
  }
  void addEntryPoint(DbgInfoIntrinsic *DII) {
    if (auto DDI = dyn_cast<DbgDeclareInst>(DII))
      addEntryPoint(DDI->getVariable());
    else if (auto DVI = dyn_cast<DbgValueInst>(DII))
      addEntryPoint(DVI->getVariable());
    else
      llvm_unreachable("invalid debug info intrinsic");
  }

  /// \brief Recursively modify reachable, unvisited DIFiletrings
  inline bool run();
};

bool DIFileRecursiveModify::run() {
  bool Changed = false;
  auto MDSEmpty = MDString::get(Ctx, "_");
  auto FileEmpty = DIFile::get(Ctx, MDSEmpty, MDSEmpty);
  while (!Worklist.empty()) {
    auto* N = Worklist.pop_back_val();
    if (!N || Visited.count(N))
      continue;
    Visited.insert(N);
    for (unsigned i = 0; i < N->getNumOperands(); ++i) {
      Metadata *MD = N->getOperand(i);
      if (!MD)
        continue;
      if (auto* DIF = dyn_cast<DIFile>(MD)) {
        N->replaceOperandWith(i, FileEmpty);
      }
      else if (auto NN = dyn_cast<MDNode>(MD))
        Worklist.push_back(NN);
    }
  }
  return Changed;
}

static MDNode *stripDebugLocFromLoopID(MDNode *N) {
  assert(N->op_begin() != N->op_end() && "Missing self reference?");

  // if there is no debug location, we do not have to rewrite this MDNode.
  if (std::none_of(N->op_begin() + 1, N->op_end(), [](const MDOperand &Op) {
        return isa<DILocation>(Op.get());
        })) 
  return N;

  // If there is only the debug location without any actual loop metadata, we
  // can remove the metadata.
  if (std::none_of(N->op_begin() + 1, N->op_end(), [](const MDOperand &Op) {
        return !isa<DILocation>(Op.get());
        })) 
  return nullptr;

  SmallVector<Metadata *, 4> Args;
  // Reserve operand 0 for loop id self reference.
  auto TempNode = MDNode::getTemporary(N->getContext(), None);
  Args.push_back(TempNode.get());
  // Add all non-debug location operands back.
  for (auto Op = N->op_begin() + 1; Op != N->op_end(); Op++) {
    if (!isa<DILocation>(*Op))
      Args.push_back(*Op);
  }

  // Set the first operand to itself.
  MDNode *LoopID = MDNode::get(N->getContext(), Args);
  LoopID->replaceOperandWith(0, LoopID);
  return LoopID;
}


struct LightDwarf: public ModulePass
{
  static char ID;

  LightDwarf():
    ModulePass(ID)
  {}

  StringRef getPassName() const override {
    return "light-dwarf";
  }

  static bool shouldStripAll(GlobalObject const& GO) {
    return GO.hasHiddenVisibility() || GO.hasLocalLinkage();
  }
  // Extracted from llvm::StripDebugInfo (exactly the same, except that we keep
  // the DISubProgram node!)
  static bool processFunction(Function& F)
  {
    if (shouldStripAll(F)) {
      return stripDebugInfo(F);
    }

    bool Changed = false;
    DenseMap<MDNode*, MDNode*> LoopIDsMap;
    for (BasicBlock &BB : F) {
      for (auto II = BB.begin(), End = BB.end(); II != End;) {
        Instruction &I = *II++; // We may delete the instruction, increment now.
        if (auto* DVI = dyn_cast<DbgValueInst>(&I)) {
          // We keep these, otherwise llvm won't emit debug infos for function parameters!
          if (isa<Argument>(DVI->getValue())) {
            continue;
          }
        }
        else
        if (isa<ReturnInst>(&I)) {
          // We should not process return instructions, otherwise we got the same issue as above (!!)
          continue;
        }

        if (isa<DbgInfoIntrinsic>(&I)) {
          I.eraseFromParent();
          Changed = true;
          continue;
        }
        if (I.getDebugLoc()) {
          Changed = true;
          I.setDebugLoc(DebugLoc());
        }
      }

      auto *TermInst = BB.getTerminator();
      if (auto *LoopID = TermInst->getMetadata(LLVMContext::MD_loop)) {
        auto *NewLoopID = LoopIDsMap.lookup(LoopID);
        if (!NewLoopID)
          NewLoopID = LoopIDsMap[LoopID] = stripDebugLocFromLoopID(LoopID);
        if (NewLoopID != LoopID)
          TermInst->setMetadata(LLVMContext::MD_loop, NewLoopID);
      }
    }
    return Changed;
  }

  static bool processGV(GlobalVariable& GV)
  {
    if (!shouldStripAll(GV)) {
      return false;
    }

    SmallVector<MDNode *, 1> MDs;
    GV.getMetadata(LLVMContext::MD_dbg, MDs);
    if (MDs.empty()) {
      return false;
    }
    GV.eraseMetadata(LLVMContext::MD_dbg);
    return true;
  }

  bool runOnModule(Module& M) override {
    bool Changed = false;

    for (auto& F: M) {
      Changed |= processFunction(F);
    }

    for (auto& GV: M.globals()) {
      Changed |= processGV(GV);
    }

    auto& Ctx = M.getContext();
    DIFileRecursiveModify Modifier(Ctx);

    DebugInfoFinder DIF;
    DIF.processModule(M);
    for (auto* CU: DIF.compile_units()) {
      Modifier.addEntryPoint(CU);
    }
    for (auto* SP: DIF.subprograms()) {
      Modifier.addEntryPoint(SP);
    }
    for (auto* Ty: DIF.types()) {
      Modifier.addEntryPoint(Ty);
    }
    for (auto* S: DIF.scopes()) {
      Modifier.addEntryPoint(S);
    }
    for (auto* GV: DIF.global_variables()) {
      Modifier.addEntryPoint(GV);
    }
    
    Changed |= Modifier.run();

    return Changed;
  }
};

char LightDwarf::ID;

void addPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
  PM.add(new LightDwarf{});
}

// Register this pass before vectorization!
RegisterStandardPasses S(PassManagerBuilder::EP_OptimizerLast,
                         addPass);

} // anonymous
