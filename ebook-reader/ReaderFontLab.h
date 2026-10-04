#pragma once
#include <stdint.h>
#include <stddef.h>
// Temporary comparison state; deliberately never changes reader preferences.
class ReaderFontLab {
  int bank_=0,index_=0;
  bool reference_=false;
public:
  void reset(){bank_=index_=0;reference_=false;}
  int bank()const{return bank_;}
  int index()const{return index_;}
  bool reference()const{return reference_;}
  int count()const{return bank_?4:6;}
  void move(int delta){index_=(index_+delta+count())%count();reference_=false;}
  void toggleBank(){bank_=1-bank_;index_=0;reference_=false;}
  void toggleReference(){reference_=!reference_;}
  bool draw(uint8_t *canvas,size_t capacity,int width,int height)const;
};
