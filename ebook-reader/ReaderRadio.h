#pragma once
// Both clients release the radio before another client starts it.
bool readerNetworkReady();
bool readerRadioStart(bool accessPoint);
void readerRadioStop();
