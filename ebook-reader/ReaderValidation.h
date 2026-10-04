#pragma once
#include "ReaderStorage.h"
// Durable, explicit QA snapshot. No restoration runs automatically during boot.
bool readerValidationBegin(ReaderStorage &storage,const std::string &session);
bool readerValidationRestore(ReaderStorage &storage,std::string &session);
void readerValidationComplete();
