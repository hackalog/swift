//===--- ToolChain.h - Collections of tools for one platform ----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_TOOLCHAIN_H
#define SWIFT_DRIVER_TOOLCHAIN_H

#include "swift/Basic/LLVM.h"
#include "swift/Driver/Action.h"
#include "swift/Frontend/Types.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Option/Option.h"

#include <memory>

namespace swift {
namespace driver {
  class CommandOutput;
  class Compilation;
  class Driver;
  class Job;
  class OutputInfo;

/// A ToolChain is responsible for turning abstract Actions into concrete,
/// runnable Jobs.
///
/// The primary purpose of a ToolChain is built around the
/// \c constructInvocation family of methods. This is a set of callbacks
/// following the Visitor pattern for the various JobAction subclasses, which
/// returns an executable name and arguments for the Job to be run. The base
/// ToolChain knows how to perform most operations, but some (like linking)
/// require platform-specific knowledge, provided in subclasses.
class ToolChain {
  const Driver &D;
  const llvm::Triple Triple;
  mutable llvm::StringMap<std::string> ProgramLookupCache;

protected:
  ToolChain(const Driver &D, const llvm::Triple &T) : D(D), Triple(T) {}

  /// A special name used to identify the Swift executable itself.
  constexpr static const char * const SWIFT_EXECUTABLE_NAME = "swift";

  /// Packs together the supplementary information about the job being created.
  class JobContext {
  private:
    Compilation &C;

    /// The limit for passing a list of files on the command line.
    static const size_t TOO_MANY_FILES = 128;

  public:
    ArrayRef<const Job *> Inputs;
    ArrayRef<const Action *> InputActions;
    const CommandOutput &Output;
    const OutputInfo &OI;

    /// The arguments to the driver. Can also be used to create new strings with
    /// the same lifetime.
    ///
    /// This just caches C.getArgs().
    const llvm::opt::ArgList &Args;

  public:
    JobContext(Compilation &C, ArrayRef<const Job *> Inputs,
               ArrayRef<const Action *> InputActions,
               const CommandOutput &Output, const OutputInfo &OI);

    /// Forwards to Compilation::getInputFiles.
    ArrayRef<InputPair> getTopLevelInputFiles() const;

    /// Forwards to Compilation::getAllSourcesPath.
    const char *getAllSourcesPath() const;

    /// Creates a new temporary file for use by a job.
    ///
    /// The returned string already has its lifetime extended to match other
    /// arguments.
    const char *getTemporaryFilePath(const llvm::Twine &name,
                                     StringRef suffix = "") const;

    /// Test the number of files to see if an input file list is needed.
    bool shouldUseInputFileList() const;

    /// Test the number of primary files to see if a primary file list is
    /// needed.
    bool shouldUsePrimaryInputFileList() const;

    /// Does this invocation need to filter out non-Swift, -SIL, or -SIB inputs?
    bool shouldFilterInputsByType() const;

    /// Returns true if merge module input file list is needed.
    bool shouldUseMergeModuleInputFileList() const;

    /// Returns true if link input file list is needed.
    bool shouldUseLinkInputFileList() const;

    /// Return true if a file list is needed for the main outputs.
    bool shouldUseMainOutputFileList() const;

    /// Test the number of files to see if the command line would be too long
    /// without using a file list for the supplementary outputs. Can only be
    /// true for batch jobs.
    bool shouldUseSupplementaryOutputFileList() const;

    const char *computeFrontendModeForCompile() const;

    void addFrontendInputAndOutputArguments(
        llvm::opt::ArgStringList &Arguments,
        std::vector<FilelistInfo> &FilelistInfos) const;

  private:
    void addFrontendInputArguments(const bool mayHavePrimaryInputs,
                                   const bool useFileList,
                                   const bool usePrimaryFileList,
                                   const bool filterByType,
                                   llvm::opt::ArgStringList &arguments) const;
    void addFrontendSupplementaryOutputArguments(
        llvm::opt::ArgStringList &arguments) const;
  };

  /// Packs together information chosen by toolchains to create jobs.
  struct InvocationInfo {
    const char *ExecutableName;
    llvm::opt::ArgStringList Arguments;
    std::vector<std::pair<const char *, const char *>> ExtraEnvironment;
    std::vector<FilelistInfo> FilelistInfos;

    InvocationInfo(const char *name, llvm::opt::ArgStringList args = {},
                   decltype(ExtraEnvironment) extraEnv = {})
      : ExecutableName(name), Arguments(std::move(args)),
        ExtraEnvironment(std::move(extraEnv)) {}
  };

  virtual InvocationInfo
  constructInvocation(const CompileJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const InterpretJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const BackendJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const MergeModuleJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const ModuleWrapJobAction &job,
                      const JobContext &context) const;

  virtual InvocationInfo
  constructInvocation(const REPLJobAction &job,
                      const JobContext &context) const;

  virtual InvocationInfo
  constructInvocation(const GenerateDSYMJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const VerifyDebugInfoJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const GeneratePCHJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const AutolinkExtractJobAction &job,
                      const JobContext &context) const;
  virtual InvocationInfo
  constructInvocation(const LinkJobAction &job,
                      const JobContext &context) const;

  /// Searches for the given executable in appropriate paths relative to the
  /// Swift binary.
  ///
  /// This method caches its results.
  ///
  /// \sa findProgramRelativeToSwiftImpl
  std::string findProgramRelativeToSwift(StringRef name) const;

  /// An override point for platform-specific subclasses to customize how to
  /// do relative searches for programs.
  ///
  /// This method is invoked by findProgramRelativeToSwift().
  virtual std::string findProgramRelativeToSwiftImpl(StringRef name) const;

public:
  virtual ~ToolChain() = default;

  const Driver &getDriver() const { return D; }
  const llvm::Triple &getTriple() const { return Triple; }

  /// Construct a Job for the action \p JA, taking the given information into
  /// account.
  ///
  /// This method dispatches to the various \c constructInvocation methods,
  /// which may be overridden by platform-specific subclasses.
  std::unique_ptr<Job> constructJob(const JobAction &JA,
                                    Compilation &C,
                                    SmallVectorImpl<const Job *> &&inputs,
                                    ArrayRef<const Action *> inputActions,
                                    std::unique_ptr<CommandOutput> output,
                                    const OutputInfo &OI) const;

  /// Return true iff the input \c Job \p A is an acceptable candidate for
  /// batching together into a BatchJob, via a call to \c
  /// constructBatchJob. This is true when the \c Job is a built from a \c
  /// CompileJobAction in a \c Compilation \p C running in \c
  /// OutputInfo::Mode::StandardCompile output mode, with a single \c TY_Swift
  /// \c InputAction.
  bool jobIsBatchable(const Compilation &C, const Job *A) const;

  /// Equivalence relation that holds iff the two input Jobs \p A and \p B are
  /// acceptable candidates for combining together into a \c BatchJob, via a
  /// call to \c constructBatchJob. This is true when each job independently
  /// satisfies \c jobIsBatchable, and the two jobs have identical executables,
  /// output types and environments (i.e. they are identical aside from their
  /// inputs).
  bool jobsAreBatchCombinable(const Compilation &C, const Job *A,
                              const Job *B) const;

  /// Construct a \c BatchJob that subsumes the work of a set of Jobs. Any pair
  /// of elements in \p Jobs are assumed to satisfy the equivalence relation \c
  /// jobsAreBatchCombinable, i.e. they should all be "the same" job in in all
  /// ways other than their choices of inputs.
  std::unique_ptr<Job> constructBatchJob(ArrayRef<const Job *> Jobs,
                                         Compilation &C) const;

  /// Return the default language type to use for the given extension.
  /// If the extension is empty or is otherwise not recognized, return
  /// the invalid type \c TY_INVALID.
  virtual types::ID lookupTypeForExtension(StringRef Ext) const;

  /// Check whether a clang library with a given name exists.
  ///
  /// \param args Invocation arguments.
  /// \param sanitizer Sanitizer name.
  /// \param shared Whether the library is shared
  virtual bool sanitizerRuntimeLibExists(const llvm::opt::ArgList &args,
                                         StringRef sanitizer,
                                         bool shared=true) const;

  /// For an index data job,
  /// only make the index-file-path ones primary.
  static bool canCompileInputArgumentBePrimary(const CommandOutput &Output,
                                               const llvm::opt::Arg &A);
};
} // end namespace driver
} // end namespace swift

#endif
