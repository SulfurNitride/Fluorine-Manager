#include "csharp_interface.h"

#include <fstream>
#include <regex>
#include <sstream>

#include <uibase/log.h>

#include "base_script.h"
#include "csharp_utils.h"

// clang-format off

#using <System.dll>

using namespace MOBase;

/**
 * This is a assembly resolve handler that does only one thing: returns the assembly
 * containing BaseScript (usually the DLL) when requested.
 *
 * I don't know why this must be done manually... But I did not find any better solution.
 */
System::Reflection::Assembly^ currentDomain_AssemblyResolve(System::Object^, System::ResolveEventArgs^ args)
{
  using namespace System::Reflection;

  try {
    Assembly^ baseScriptAssembly = System::Reflection::Assembly::GetAssembly(BaseScript::typeid);
    if (args->Name->Equals(baseScriptAssembly->FullName)) {
      return baseScriptAssembly;
    }
  }
  catch (...)
  {
  }

  return nullptr;
}

IPluginInstaller::EInstallResult executeScript(System::String^ script) {

  using namespace System;
  using namespace System::CodeDom;
  using namespace System::CodeDom::Compiler;
  using namespace System::Collections::Generic;

  AppDomain^ currentDomain = AppDomain::CurrentDomain;
  currentDomain->AssemblyResolve += gcnew ResolveEventHandler(currentDomain_AssemblyResolve);

  // From Nexus-Mods/fomod-installer:
  Dictionary<String^, String^>^ dicOptions = gcnew Dictionary<String^, String^>(10);
  dicOptions->Add("CompilerVersion", "v4.0");

  // List of assemblies (including BaseScript) - From Nexus-Mods/fomod-installer:
  array<String^>^ referenceAssemblies = {
    "System.dll",
    "System.Runtime.dll",
    "System.Drawing.dll",
    "System.Windows.Forms.dll",
    "System.Xml.dll",
    System::Reflection::Assembly::GetAssembly(BaseScript::typeid)->Location
  };

  CompilerParameters^ cp = gcnew CompilerParameters(referenceAssemblies);
  cp->GenerateExecutable = false;
  cp->IncludeDebugInformation = false;
  cp->GenerateInMemory = true;
  cp->TreatWarningsAsErrors = false;
  CodeDomProvider^ provider = CodeDomProvider::CreateProvider("CSharp", dicOptions);

  // Compile the script
  auto result = provider->CompileAssemblyFromSource(cp, script);

  int errorCount = 0;
  for each (CompilerError ^ error in result->Errors) {
    if (error->IsWarning) {
      log::warn("C# [{}]: {}", error->Line, CSharp::to_string(error->ErrorText));
    }
    else {
      log::error("C# [{}]: {}", error->Line, CSharp::to_string(error->ErrorText));
      ++errorCount;
    }
  }

  if (errorCount > 0) {
    return IPluginInstaller::EInstallResult::RESULT_FAILED;
  }

  // Execute the script:
  try {
    auto scriptClass = result->CompiledAssembly->GetType("Script");
    BaseScript^ scriptObject = (BaseScript^)System::Activator::CreateInstance(scriptClass);
    auto onActivateMethod = scriptObject->GetType()->GetMethod("OnActivate");

    auto success = (bool)(onActivateMethod->IsStatic ? onActivateMethod->Invoke(nullptr, nullptr) : onActivateMethod->Invoke(scriptObject, nullptr));
    return success ? IPluginInstaller::EInstallResult::RESULT_SUCCESS : IPluginInstaller::EInstallResult::RESULT_CANCELED;
  }
  catch (System::Exception^ ex) {
    log::error("C# ({}): {}\n{}", CSharp::to_string(ex->GetType()->FullName), CSharp::to_string(ex->Message), CSharp::to_string(ex->StackTrace));
    System::Exception^ innerEx = ex->InnerException;
    if (innerEx) {
      log::error("C# ({}): {}\n{}", CSharp::to_string(innerEx->GetType()->FullName), CSharp::to_string(innerEx->Message), CSharp::to_string(innerEx->StackTrace));
    }
    return IPluginInstaller::EInstallResult::RESULT_FAILED;
  }

}

namespace CSharp {

  IPluginInstaller::EInstallResult executeCSharpScript(QString scriptPath, std::shared_ptr<IFileTree>& tree) {

    using namespace System;
    using namespace System::IO;
    using namespace System::Text::RegularExpressions;

    // Note: Using C# stuff here to mimicate NMM since there are some encoding issues, and
    // some regex do not work in C++:
    array<Byte>^ scriptBytes = File::ReadAllBytes(from_string(scriptPath.toStdWString()));

    // Read the script (using C# to "auto-detect" encoding in a C# way):
    String^ script;
    {
      auto memoryStream = gcnew MemoryStream(scriptBytes);
      auto reader = gcnew StreamReader(memoryStream, true);

      script = reader->ReadToEnd();

      reader->Close();
      memoryStream->Close();

      delete reader;
      delete memoryStream;
    }


    Regex^ regScriptClass = gcnew Regex(R"re((class\s+Script\s*:.*?)(\S*BaseScript))re");
    Regex^ regFommUsing = gcnew Regex(R"re(\s*using\s*fomm.Scripting\s*;)re");

    String^ strBaseScriptClassName = regScriptClass->Match(script)->Groups[2]->ToString();
    Regex^ regOtherScriptClasses = gcnew Regex(String::Format(R"re((class\s+\S+\s*:.*?)(?<!\w){0})re", strBaseScriptClassName));
    String^ strCode = script;
    strCode = regScriptClass->Replace(strCode, "$1BaseScript");
    strCode = regOtherScriptClasses->Replace(strCode, "$1BaseScript");
    strCode = regFommUsing->Replace(strCode, "");

    auto result = executeScript(strCode);

    if (result != IPluginInstaller::EInstallResult::RESULT_SUCCESS) {
      return result;
    }

    return postInstall(tree);
  }

}
