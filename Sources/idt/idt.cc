// Copyright (c) 2021 Saleem Abdulrasool.  All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace idt {
llvm::cl::OptionCategory category{"iterface definition scanner options"};
}

namespace {
// TODO(compnerd) make this configurable via a configuration file or commandline
const std::set<std::string> kIgnoredFunctions{
  "_BitScanForward",
  "_BitScanForward64",
  "_BitScanReverse",
  "_BitScanReverse64",
  "__builtin_strlen",
};

llvm::cl::opt<std::string>
export_macro("export-macro",
             llvm::cl::desc("The macro to decorate interfaces with"),
             llvm::cl::value_desc("define"), llvm::cl::Required,
             llvm::cl::cat(idt::category));

llvm::cl::opt<bool>
apply_fixits("apply-fixits", llvm::cl::init(false),
             llvm::cl::desc("Apply suggested changes to decorate interfaces"),
             llvm::cl::cat(idt::category));

llvm::cl::opt<bool>
inplace("inplace", llvm::cl::init(false),
        llvm::cl::desc("Apply suggested changes in-place"),
        llvm::cl::cat(idt::category));

llvm::cl::opt<bool>
annotateClasses("annotate-classes", llvm::cl::init(true),
                 llvm::cl::desc("Annotate classes but not their members"),
                 llvm::cl::cat(idt::category));

template <typename Key, typename Compare, typename Allocator>
bool contains(const std::set<Key, Compare, Allocator>& set, const Key& key) {
  return set.find(key) != set.end();
}
}

namespace idt {
class visitor : public clang::RecursiveASTVisitor<visitor> {
  clang::ASTContext &context_;
  clang::SourceManager &source_manager_;

  clang::DiagnosticBuilder
  unexported_public_interface(clang::SourceLocation location) {
    clang::DiagnosticsEngine &diagnostics_engine = context_.getDiagnostics();

    static unsigned kID =
        diagnostics_engine.getCustomDiagID(clang::DiagnosticsEngine::Remark,
                                           "unexported public interface %0");

    return diagnostics_engine.Report(location, kID);
  }

  clang::DiagnosticBuilder
  exported_private_interface(clang::SourceLocation location) {
    clang::DiagnosticsEngine &diagnostics_engine = context_.getDiagnostics();

    static unsigned kID =
        diagnostics_engine.getCustomDiagID(clang::DiagnosticsEngine::Remark,
                                           "exported private interface %0");

    return diagnostics_engine.Report(location, kID);
  }

  template <typename Decl_>
  inline clang::FullSourceLoc get_location(const Decl_ *TD) const {
    return context_.getFullLoc(TD->getBeginLoc()).getExpansionLoc();
  }

public:
  explicit visitor(clang::ASTContext &context)
      : context_(context), source_manager_(context.getSourceManager()) {}

  bool VisitFunctionDecl(clang::FunctionDecl *FD) {
    clang::FullSourceLoc location = get_location(FD);
    //llvm::dbgs() << location << "\n";

    llvm::dbgs() << "FunctionDecl: " << FD->getNameAsString() << "\n";

    llvm::StringRef filename = source_manager_.getFilename(location);
    if (filename.find("/lib/") != llvm::StringRef::npos ||
        filename.find("/tools/") != llvm::StringRef::npos ||
        filename.find(".def") != llvm::StringRef::npos)
      return true;
    // Ignore declarations from the system.
    if (source_manager_.isInSystemHeader(location))
      return true;

    llvm::dbgs() << "Not in system neader\n";
    // We are only interested in non-dependent types.
    if (FD->isDependentContext())
      return true;

    llvm::dbgs() << "Not dependent context\n";
    // If the function has a body, it can be materialized by the user.
    if (FD->doesThisDeclarationHaveABody())
      return true;

    llvm::dbgs() << "Not has body\n";
    // Ignore friend declarations.
    if (llvm::isa<clang::FriendDecl>(FD))
      return true;

    llvm::dbgs() << "Friend kind: " << FD->getFriendObjectKind() << "\n";
    if (FD->getFriendObjectKind() != clang::Decl::FriendObjectKind::FOK_None)
        return true;

    llvm::dbgs() << "Not friend\n";
    // Ignore deleted and defaulted functions (e.g. operators).
    if (FD->isDeleted() || FD->isDefaulted())
      return true;

    llvm::dbgs() << "Not deleted\n";
    if (const auto *MD = llvm::dyn_cast<clang::CXXMethodDecl>(FD)) {
      // Skip class members if we are only annotating classes. 
      if (annotateClasses)
        return true;
      // Ignore private members (except for a negative check).
      if (MD->getAccess() == clang::AccessSpecifier::AS_private) {
        // TODO(compnerd) this should also handle `__visibility__`
        if (MD->hasAttr<clang::DLLExportAttr>())
          // TODO(compnerd) this should emit a fix-it to remove the attribute
          exported_private_interface(location) << MD;
        return true;
      }
    llvm::dbgs() << "Not Method\n";

      // Pure virtual methods cannot be exported.
      if (MD->isPure())
        return true;
    }
    llvm::dbgs() << "Not Pure\n";

    // If the function has a dll-interface, it is properly annotated.
    // TODO(compnerd) this should also handle `__visibility__`
    if (FD->hasAttr<clang::DLLExportAttr>() ||
        FD->hasAttr<clang::DLLImportAttr>() ||
        FD->hasAttr<clang::VisibilityAttr>())
      return true;

    llvm::dbgs() << "No attribute\n";
    // Ignore known forward declarations (builtins)
    // TODO(compnerd) replace with std::set::contains in C++20
    if (contains(kIgnoredFunctions, FD->getNameAsString()))
      return true;
    llvm::dbgs() << "Not ignored.\n";

    clang::SourceLocation insertion_point =
        FD->getTemplatedKind() == clang::FunctionDecl::TK_NonTemplate
            ? FD->getBeginLoc()
            : FD->getInnerLocStart();
    unexported_public_interface(location)
        << FD
        << clang::FixItHint::CreateInsertion(insertion_point,
                                             export_macro + " ");
    return true;
  }
  
  bool VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl *CTSD) {
    llvm::dbgs() << "TemplateDecl: " << CTSD->getNameAsString() << "\n";
    clang::FullSourceLoc location = get_location(CTSD);
    location.dump();
    llvm::StringRef filename = source_manager_.getFilename(location);
    if (filename.find("/lib/") != llvm::StringRef::npos ||
        filename.find("/tools/") != llvm::StringRef::npos ||
        filename.find(".def") != llvm::StringRef::npos)
      return true;
    llvm::dbgs() << "In correct spot\n";
    if (CTSD->hasAttr<clang::DLLExportAttr>() ||
        CTSD->hasAttr<clang::DLLImportAttr>() ||
        CTSD->hasAttr<clang::VisibilityAttr>())
      return true; 
    llvm::dbgs() << "No visibility\n";
  
//    if (CTSD->isThisDeclarationADefinition())
//      return true;

    int offset = 6;
    if (CTSD->getSpecializedTemplate()->getTemplatedDecl()->isStruct())
      offset = 7;
    clang::SourceLocation insertion_point = CTSD->getExternLoc();
    unexported_public_interface(location)
        << CTSD
        << clang::FixItHint::CreateInsertion(insertion_point,
                                             export_macro + " ");
    return true;
    
  }
  
  bool VisitVarDecl(clang::VarDecl *VD) {
    clang::FullSourceLoc location = get_location(VD);
    llvm::StringRef filename = source_manager_.getFilename(location);
    if (filename.find("/lib/") != llvm::StringRef::npos ||
        filename.find("/tools/") != llvm::StringRef::npos ||
        filename.find(".def") != llvm::StringRef::npos)
      return true;
    if (VD->hasAttr<clang::DLLExportAttr>() ||
        VD->hasAttr<clang::DLLImportAttr>() ||
        VD->hasAttr<clang::VisibilityAttr>())
      return true; 
   
    if (!VD->hasExternalStorage())
      return true; 
    clang::SourceLocation insertion_point = VD->getBeginLoc();
    unexported_public_interface(location)
        << VD
        << clang::FixItHint::CreateInsertion(insertion_point,
                                             export_macro + " ");
    return true;
  }

  bool VisitCXXRecordDecl(clang::CXXRecordDecl *CD) {
    llvm::dbgs() << "RecordDecl: " << CD->getNameAsString() << "\n";
    if (!annotateClasses)
      return true;

    if (CD->getNameAsString() == "LLVM_ABI")
      return true;
    clang::FullSourceLoc location = get_location(CD);
    llvm::StringRef filename = source_manager_.getFilename(location);
    if (filename.find("/lib/") != llvm::StringRef::npos ||
        filename.find("/tools/") != llvm::StringRef::npos ||
        filename.find(".def") != llvm::StringRef::npos)
      return true;
    // Ignore declarations from the system.
    if (source_manager_.isInSystemHeader(location))
      return true;

    llvm::dbgs() << "NOt in system header\n";
    if (!CD->isCompleteDefinition())
      return true;

     llvm::dbgs() << "Is a complete devinition\n";

    // We don't want to annotate nested classes.
    if (llvm::isa<clang::RecordDecl>(CD->getParent()))
      return true;
    
    llvm::dbgs() << "NOt a nested class\n";
    llvm::dbgs() << "Export: " << CD->hasAttr<clang::DLLExportAttr>() << " IMPORT: " << CD->hasAttr<clang::DLLImportAttr>() << " VISIBLITY: " << CD->hasAttr<clang::VisibilityAttr>() << "\n";
    if (CD->hasAttr<clang::DLLExportAttr>() ||
        CD->hasAttr<clang::DLLImportAttr>() ||
        CD->hasAttr<clang::VisibilityAttr>())
      return true; 

    llvm::dbgs() << "No visibility\n";
    if (CD->isUnion())
       return true;

    llvm::dbgs() << "No Union\n";
    // Only annotate classes in headers.
    if (source_manager_.getIncludeLoc(source_manager_.getFileID(CD->getBeginLoc())).isInvalid())
       return true;

    llvm::dbgs() << "In Header\n";
    llvm::dbgs() << "Template kind: " << CD->getTemplateSpecializationKind() << "\n";
    if (CD->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
        CD->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition)
      return true;

    llvm::dbgs() << "Applying record fixup\n";
    // FIXME(tstellar) There must be a better way of getting an insertion point
    // after the class keyword.
    int offset = 6;
    if (CD->isStruct())
      offset = 7;
    clang::SourceLocation insertion_point = CD->getBeginLoc().getLocWithOffset(offset);
    unexported_public_interface(location)
        << CD
        << clang::FixItHint::CreateInsertion(insertion_point,
                                             export_macro + " ");
    return true;
  }
};

class consumer : public clang::ASTConsumer {
  struct fixit_options : clang::FixItOptions {
    fixit_options() {
      InPlace = inplace;
      Silent = apply_fixits;
    }

    std::string RewriteFilename(const std::string &filename, int &fd) override {
      //llvm_unreachable("unexpected call to RewriteFilename");
      return "";
    }
  };

  idt::visitor visitor_;

  fixit_options options_;
  std::unique_ptr<clang::FixItRewriter> rewriter_;

public:
  explicit consumer(clang::ASTContext &context)
      : visitor_(context) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    if (apply_fixits) {
      clang::DiagnosticsEngine &diagnostics_engine = context.getDiagnostics();
      rewriter_ =
          std::make_unique<clang::FixItRewriter>(diagnostics_engine,
                                                 context.getSourceManager(),
                                                 context.getLangOpts(),
                                                 &options_);
      diagnostics_engine.setClient(rewriter_.get(), /*ShouldOwnClient=*/false);
    }

    visitor_.TraverseDecl(context.getTranslationUnitDecl());

    if (apply_fixits)
      rewriter_->WriteFixedFiles();
  }
};

struct action : clang::ASTFrontendAction {
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI, llvm::StringRef) override {
    return std::make_unique<idt::consumer>(CI.getASTContext());
  }
};

struct factory : clang::tooling::FrontendActionFactory {
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<idt::action>();
  }
};
}

int main(int argc, char *argv[]) {
  using namespace clang::tooling;

  auto options =
      CommonOptionsParser::create(argc, const_cast<const char **>(argv),
                                  idt::category, llvm::cl::OneOrMore);
  if (options) {
    ClangTool tool{options->getCompilations(), options->getSourcePathList()};
    return tool.run(new idt::factory{});
  } else {
    llvm::logAllUnhandledErrors(std::move(options.takeError()), llvm::errs());
    return EXIT_FAILURE;
  }
}
