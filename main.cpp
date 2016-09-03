#include <iostream>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Rewrite/Rewriter.h"
#include "llvm/Config/config.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Sema/Sema.h"

using namespace llvm;
using namespace clang;

//#include "MyConsumer.h"
#include "BasicASTConsumer.h"

class ASTRewriter {

  std::string                             _inFilename;
  std::string                             _outFilename;

  LangOptions                             _langOpts;
  TargetOptions                           _tarOpts;
  FileSystemOptions                       _fsOpts;
  llvm::OwningPtr<Diagnostic>             _diags;
  llvm::OwningPtr<SourceManager>          _sm;
  llvm::OwningPtr<FileManager>            _fm;
  llvm::OwningPtr<TargetInfo>             _ti;
  llvm::OwningPtr<Preprocessor>           _pp;
  llvm::OwningPtr<llvm::raw_fd_ostream>   _fos;
  llvm::OwningPtr<HeaderSearch>           _headers;
  llvm::OwningPtr<ASTContext>             _context;
  llvm::OwningPtr<ASTConsumer>            _consumer;
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> _diagnosticIds;

public:

  ASTRewriter(const std::string &inFilename, const std::string &outFilename) :
    _inFilename(inFilename), _outFilename(outFilename),_diagnosticIds(new DiagnosticIDs) {    
    _fm.reset(new FileManager(_fsOpts));
    
    _diags.reset(new Diagnostic(_diagnosticIds));
    _diags->setSuppressAllDiagnostics(true);	// FIXME diagnostics errors
    _sm.reset(new SourceManager(*_diags,*_fm.get()));
    _headers.reset(new HeaderSearch(*_fm));

    // Setting the target machine properties
    _tarOpts.Triple = LLVM_HOSTTRIPLE; // Should be changed to enable cross-platform
    _tarOpts.ABI = "";
    _tarOpts.CPU = "";
    _tarOpts.Features.clear();
    _langOpts.CPlusPlus = true;
    _ti.reset(TargetInfo::CreateTargetInfo(*_diags, _tarOpts));
    _pp.reset(new Preprocessor(*_diags, _langOpts, *_ti, *_sm, *_headers));
    
    // create output stream
    string errors;
    _fos.reset(new raw_fd_ostream(_outFilename.c_str(), errors));
    //_consumer.reset(new MyConsumer(_inFilename, _fos.get(), *_diags, _langOpts));
    //_consumer.reset(CreateASTDumper());
    _consumer.reset(new BasicASTConsumer(_inFilename, _fos.get(), *_diags, _langOpts));
  }


  int rewrite() {

    // Add input file
    const FileEntry* file = _fm->getFile(_inFilename);
    if (!file) {
      cerr << "Failed to open \'" << _inFilename << "\'" << endl;
      return EXIT_FAILURE;
    }
    FileID fileId = _sm->createMainFileID(file);

    // do the parsing
    IdentifierTable idTable(_langOpts);
    SelectorTable selTable;
    Builtin::Context builtinCtx(*_ti);
    _context.reset(new ASTContext(_langOpts, *_sm, *_ti,
                              idTable, selTable, builtinCtx, 0));
    _consumer->Initialize(*_context);
    Sema sema(*_pp, *_context, *_consumer.get());
    
    ParseAST(sema);   
   // ParseAST(*_pp, _consumer.get(), *_context);
    return EXIT_SUCCESS;
  }
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "clangtest [filename]" << std::endl;
    return 1;
  }
  std::string outFileName = argv[1];
  outFileName += ".tmp";
  ASTRewriter rewriter(argv[1], outFileName);
  return rewriter.rewrite();
}
