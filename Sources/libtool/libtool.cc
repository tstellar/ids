// Copyright (c) 2021 Saleem Abdulrasool.  All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/Path.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace libtool {
llvm::cl::OptionCategory category{"libtool options"};

class visitor : public clang::RecursiveASTVisitor<visitor> {
  clang::ASTContext &context_;
  clang::SourceManager &source_manager_;

  llvm::DenseSet<const clang::NamedDecl *> decls_;

  void diagnose(const clang::NamedDecl *ND, clang::FullSourceLoc location) {
    ND->printQualifiedName(llvm::outs());

    llvm::StringRef filename = source_manager_.getFilename(location);
    // NOTE(compnerd) hardcode 261 to avoid Windows.h dependency
    llvm::SmallVector<char, 261> buffer;
    llvm::sys::path::native(filename, buffer);

    llvm::outs() << " at " << buffer << ":"
                 << location.getExpansionLineNumber() << '\n';
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

    // Ignore declarations from the system.
    if (source_manager_.isInSystemHeader(location))
      return true;

    // If the function has a body, it can be materialized by the user.
    if (FD->hasBody())
      return true;

    // Let `VisitCXXMethodDecl` handle `CXXMethodDecl`s
    if (llvm::isa<clang::CXXMethodDecl>(FD))
      return true;

    // If the function has a dll-interface, it is properly annotated.
    if (FD->hasAttr<clang::DLLExportAttr>() ||
        FD->hasAttr<clang::DLLImportAttr>())
      return true;

    static const std::set<std::string> kIgnoredFunctions{
      "_BitScanForward",
      "_BitScanForward64",
      "_BitScanReverse",
      "_BitScanReverse64",
      "__builtin_strlen",
    };

    // Known Forward Declarations
    if (kIgnoredFunctions.find(FD->getNameAsString()) != kIgnoredFunctions.end())
      return true;

    diagnose(FD, location);
    return true;
  }

  bool VisitCXXMethodDecl(clang::CXXMethodDecl *MD) {
    clang::FullSourceLoc location = get_location(MD);

    // Ignore declarations from the system.
    if (source_manager_.isInSystemHeader(location))
      return true;

    // We are only interested in non-dependent types.
    if (MD->isDependentContext())
      return true;

    // If the method has a body, it can be materialized by the user.
    if (MD->hasBody())
      return true;

    // Ignore deleted and defaulted members.
    if (MD->isDeleted() || MD->isDefaulted())
      return true;

    // Methods which are explicitly exported are properly annotated.
    if (MD->hasAttr<clang::DLLExportAttr>() ||
        MD->hasAttr<clang::DLLImportAttr>())
      return true;

    if (MD->getAccess() == clang::AccessSpecifier::AS_private &&
        MD->hasAttr<clang::DLLExportAttr>()) {
      llvm::outs() << "WARNING: over-exporting ";
      diagnose(MD, location);
    }

    const clang::CXXRecordDecl *RD = MD->getParent()->getCanonicalDecl();

    diagnose(MD, location);
    return true;
  }
};

class consumer : public clang::ASTConsumer {
  clang::ASTContext &context_;
  libtool::visitor visitor_;

public:
  explicit consumer(clang::ASTContext &context)
      : context_(context), visitor_(context) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    visitor_.TraverseDecl(context.getTranslationUnitDecl());
    // visitor_.diagnose();
  }
};

class action : public clang::ASTFrontendAction {
public:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI, llvm::StringRef) override {
    return std::make_unique<libtool::consumer>(CI.getASTContext());
  }
};
}

int main(int argc, char *argv[]) {
  auto options =
      clang::tooling::CommonOptionsParser::create(argc,
                                                  const_cast<const char **>(argv),
                                                  libtool::category,
                                                  llvm::cl::NumOccurrencesFlag::OneOrMore);
  if (options) {
    clang::tooling::ClangTool tool{options->getCompilations(), options->getSourcePathList()};
    return tool.run(clang::tooling::newFrontendActionFactory<libtool::action>().get());
  } else {
    llvm::logAllUnhandledErrors(std::move(options.takeError()), llvm::errs());
    return EXIT_FAILURE;
  }
}