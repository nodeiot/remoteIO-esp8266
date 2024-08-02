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

void setup() 
{
  device1.begin();
}

void loop() 
{
  device1.loop();
}
