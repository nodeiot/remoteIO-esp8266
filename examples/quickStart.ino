/*
######################################################################
##      Integração das tecnologias da REMOTE IO com Node IOT        ##
##                          Version 1.0                             ##
##   Código base para implementação de projetos de digitalização de ##
##   processos, automação, coleta de dados e envio de comandos com  ##
##   controle embarcado e na nuvem.                                 ##
##                                                                  ##
######################################################################
*/

#include <ESP8266RemoteIO.h>

RemoteIO device1;

// Define your own callback function to handle communication events with NodeIoT
// Example:
void myCallback(String ref, String value)
{
/* 
  if (ref == "turnOnLight")
  {
    device1.updatePinOutput(ref);        // This function will update the IO pin linked to 'turnOnLight' variable on previously done NodeIoT device configuration
  }
  
*/
  
  ///Serial.printf("[myCallback] ref: %s, value: %s\n", ref, value);
}

void setup() 
{
  device1.begin(myCallback);
}

void loop() 
{
  device1.loop();
}