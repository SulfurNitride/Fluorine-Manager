library libbsarch;

uses
  Math,
  Classes,
  Types,
  SysUtils,
  wbStreams,
  wbBSArchive;

type
  TwbBSResultCode = (
    BSA_RESULT_NONE = 0,
    BSA_RESULT_EXCEPTION = -1
  );
  TwbBSResultMessage = packed record
    code: ShortInt;
    text: array[0..1023] of WideChar;
  end;
  TwbBSResultMessageBuffer = packed record
    buffer: TwbBSResultBuffer;
    message: TwbBSResultMessage;
  end;

{Shared}

function string_to_cstring(
  aString: String;
  const aStringBufferSize: Cardinal;
  const aStringBuffer: PChar
): Integer; stdcall;
begin
  Result := Min(aStringBufferSize, Length(AString));
  if (aStringBuffer <> nil) and (Result > 0) then
    Move(AString[1], aStringBuffer^, Result * SizeOf(WideChar));
end;

function string_to_cstring_end(
  aString: String;
  const aStringBufferSize: Cardinal;
  const aStringBuffer: PChar
): Integer; stdcall;
begin
  Result := Min(aStringBufferSize - 1, Length(AString));
  if (aStringBuffer <> nil) and (Result > 0) then
  begin
    Move(AString[1], aStringBuffer^, Result * SizeOf(WideChar));
    aStringBuffer[Result] := #0;
  end;
end;

procedure exception_handler(E: Exception; var Result: TwbBSResultMessage); stdcall;
begin
  Result.code := ShortInt(BSA_RESULT_EXCEPTION);
  string_to_cstring_end(E.Message, Length(Result.text), Result.text);
end;

procedure buffer_exception_handler(E: Exception; var Result: TwbBSResultMessageBuffer); stdcall;
begin
  Result.buffer.size := 0;
  Result.buffer.data := nil;
  Result.message.code := ShortInt(BSA_RESULT_EXCEPTION);
  string_to_cstring_end(E.Message, Length(Result.message.text), Result.message.text);
end;

{BSA File List}

function bsa_entry_list_create:Pointer; stdcall;
begin
  try
    Result := TStringList.Create;
  except
    Result := nil;
  end;
end;

function bsa_entry_list_free(obj: Pointer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TStringList(obj).Clear();
    TStringList(obj).free;
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_entry_list_count(obj: Pointer): Integer; stdcall;
begin
  try
    Result := TStringList(obj).Count;
  except
    Result := Ord(BSA_RESULT_EXCEPTION);
  end;
end;

function bsa_entry_list_add(obj: Pointer; const aEntryString: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TStringList(obj).Add(String(aEntryString));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_entry_list_get(obj: Pointer; const aIndex: Cardinal; const aStringBufferSize: Cardinal; const aStringBuffer: PChar): Integer; stdcall;
begin
  try
    Result := string_to_cstring_end(TStringList(obj).Strings[aIndex], aStringBufferSize, aStringBuffer);
  except
    Result := Integer(BSA_RESULT_EXCEPTION);
  end;
end;

{BSArchive}

function bsa_create:Pointer; stdcall;
begin
  try
    Result := TwbBSArchive.Create;
  except
    Result := nil;
  end;
end;

function bsa_free(obj: Pointer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).free;
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_load_from_file(obj: Pointer; const aFilePath: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).LoadFromFile(String(aFilePath));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_create_archive(obj: Pointer; const aFilePath: PChar; aType: TBSArchiveType; aFileList: TStringList): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).CreateArchive(String(aFilePath), aType, aFileList);
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_save(obj: Pointer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).Save;
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_add_file_from_disk_root(obj: Pointer; const aRootDir, aSourcePath: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).AddFileDiskRoot(String(aRootDir), String(aSourcePath));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_add_file_from_disk(obj: Pointer; const aFilePath, aSourcePath: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).AddFileDisk(String(aFilePath), String(aSourcePath));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_add_file_from_memory(obj: Pointer; const aFilePath: PChar; const aSize: Cardinal; const aData: PByte): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).AddFileData(String(aFilePath), aSize, aData);
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_find_file_record(obj: Pointer; const aFilePath: PChar): Pointer; stdcall;
begin
  try
    Result := TwbBSArchive(obj).FindFileRecord(String(aFilePath));
  except
    Result := nil
  end;
end;

function bsa_extract_file_data_by_record(obj: Pointer; aFileRecord: Pointer): TwbBSResultMessageBuffer; stdcall;
begin
  Result.message.code := Ord(BSA_RESULT_NONE);
  try
    Result.buffer := TwbBSArchive(obj).ExtractFileData(aFileRecord);
  except
    on E: Exception do
      buffer_exception_handler(E, Result);
  end;
end;

function bsa_extract_file_data_by_filename(obj: Pointer; const aFilePath: PChar): TwbBSResultMessageBuffer; stdcall;
begin
  Result.message.code := Ord(BSA_RESULT_NONE);
  try
    Result.buffer := TwbBSArchive(obj).ExtractFileData(String(aFilePath));
  except
    on E: Exception do
      buffer_exception_handler(E, Result);
  end;
end;

function bsa_file_data_free(obj: Pointer; fileDataResult: TwbBSResultMessageBuffer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).ReleaseFileData(fileDataResult.buffer);
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_extract_file(obj: Pointer; const aFilePath, aSaveAs: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).ExtractFile(String(aFilePath), String(aSaveAs));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_iterate_files(obj: Pointer; aProc: TBSFileIterationProc; aContext: Pointer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).IterateFiles(aProc, aContext);
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_file_exists(obj: Pointer; const aFilePath: string): Boolean; stdcall;
begin
  try
    Result := TwbBSArchive(obj).FileExists(aFilePath);
  except
    Result := False;
  end;
end;

function bsa_get_resource_list(obj: Pointer; const aEntryResultList: TStringList; aFolder: PChar): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).ResourceList(aEntryResultList, String(aFolder));
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_close(obj: Pointer): TwbBSResultMessage; stdcall;
begin
  Result.code := Ord(BSA_RESULT_NONE);
  try
    TwbBSArchive(obj).Close;
  except
    on E: Exception do
      exception_handler(E, Result);
  end;
end;

function bsa_filename_get(obj: Pointer; const aStringBufferSize: Cardinal; const aStringBuffer: PChar): Cardinal; stdcall;
begin
  Result := string_to_cstring_end(TwbBSArchive(obj).FileName, aStringBufferSize, aStringBuffer);
end;

function bsa_archive_type_get(obj: Pointer): TBSArchiveType; stdcall;
begin
  Result := TwbBSArchive(obj).ArchiveType;
end;

function bsa_version_get(obj: Pointer): Cardinal; stdcall;
begin
  Result := TwbBSArchive(obj).Version;
end;

function bsa_format_name_get(obj: Pointer; const aStringBufferSize: Cardinal; const aStringBuffer: PChar): Cardinal; stdcall;
begin
  Result := string_to_cstring_end(TwbBSArchive(obj).FormatName, aStringBufferSize, aStringBuffer);
end;

function bsa_file_count_get(obj: Pointer): Cardinal; stdcall;
begin
  Result := TwbBSArchive(obj).FileCount;
end;

function bsa_archive_flags_get(obj: Pointer): Cardinal; stdcall;
begin
  Result := TwbBSArchive(obj).ArchiveFlags;
end;

procedure bsa_archive_flags_set(obj: Pointer; flags: Cardinal); stdcall;
begin
  TwbBSArchive(obj).ArchiveFlags := flags;
end;

function bsa_file_flags_get(obj: Pointer): Cardinal; stdcall;
begin
  Result := TwbBSArchive(obj).FileFlags;
end;

procedure bsa_file_flags_set(obj: Pointer; flags: Cardinal); stdcall;
begin
  TwbBSArchive(obj).FileFlags := flags;
end;

function bsa_compress_get(obj: Pointer): Boolean; stdcall;
begin
  Result := TwbBSArchive(obj).Compress;
end;

procedure bsa_compress_set(obj: Pointer; compress: Boolean); stdcall;
begin
  TwbBSArchive(obj).Compress := compress;
end;

function bsa_share_data_get(obj: Pointer): Boolean; stdcall;
begin
  Result := TwbBSArchive(obj).ShareData;
end;

procedure bsa_share_data_set(obj: Pointer; shareData: Boolean); stdcall;
begin
  TwbBSArchive(obj).ShareData := shareData;
end;

procedure bsa_file_dds_info_callback_set(obj: Pointer; aProc: TBSFileDDSInfoProc; aContext: Pointer); stdcall;
begin
  TwbBSArchive(obj).DDSInfoProc := aProc;
  TwbBSArchive(obj).DDSInfoProcContext := aContext;
end;

exports
  bsa_entry_list_create,
  bsa_entry_list_free,
  bsa_entry_list_count,
  bsa_entry_list_add,
  bsa_entry_list_get,

  bsa_filename_get,
  bsa_archive_type_get,
  bsa_version_get,
  bsa_format_name_get,
  bsa_file_count_get,
  bsa_archive_flags_get,
  bsa_archive_flags_set,
  bsa_file_flags_get,
  bsa_file_flags_set,
  bsa_compress_get,
  bsa_compress_set,
  bsa_share_data_get,
  bsa_share_data_set,
  bsa_file_dds_info_callback_set,

  bsa_close,
  bsa_get_resource_list,
  bsa_file_exists,
  bsa_iterate_files,
  bsa_extract_file,
  bsa_file_data_free,
  bsa_extract_file_data_by_filename,
  bsa_extract_file_data_by_record,
  bsa_find_file_record,
  bsa_add_file_from_memory,
  bsa_add_file_from_disk_root,
  bsa_add_file_from_disk,
  bsa_save,
  bsa_create_archive,
  bsa_load_from_file,
  bsa_create,
  bsa_free
  ;
end.
