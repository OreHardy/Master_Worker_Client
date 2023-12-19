#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "utils.h"
#include "myassert.h"

#include "master_worker.h"


/************************************************************************
 * Données persistantes d'un worker
 ************************************************************************/
typedef struct
{
    // données internes (valeur de l'élément, cardinalité)
    float element;
    int nbOfElement; 
    // communication avec le père (2 tubes) et avec le master (1 tube en écriture)
    int fdIn; 
    int fdOut;
    int fdToMaster;
    // communication avec le fils gauche s'il existe (2 tubes)
    int fdFromSubleft;
    int fdToSubleft;
    // communication avec le fils droit s'il existe (2 tubes)
     int fdFromSubright;
     int fdToSubright;
} Data;


/************************************************************************
 * Usage et analyse des arguments passés en ligne de commande
 ************************************************************************/
static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usage : %s <elt> <fdIn> <fdOut> <fdToMaster>\n", exeName);
    fprintf(stderr, "   <elt> : élément géré par le worker\n");
    fprintf(stderr, "   <fdIn> : canal d'entrée (en provenance du père)\n");
    fprintf(stderr, "   <fdOut> : canal de sortie (vers le père)\n");
    fprintf(stderr, "   <fdToMaster> : canal de sortie directement vers le master\n");
    if (message != NULL)
        fprintf(stderr, "message : %s\n", message);
    exit(EXIT_FAILURE);
}

static void parseArgs(int argc, char * argv[], Data *data)
{
    myassert(data != NULL, "il faut l'environnement d'exécution");

    if (argc != 5)
        usage(argv[0], "Nombre d'arguments incorrect");

    //initialisation data
    float elt = strtof(argv[1], NULL);
    int fdIn = strtol(argv[2], NULL, 10);
    int fdOut = strtol(argv[3], NULL, 10);
    int fdToMaster = strtol(argv[4], NULL, 10);
    //printf("%g %d %d %d\n", elt, fdIn, fdOut, fdToMaster);

    data->element=elt;
    data->nbOfElement=1;
    data->fdIn=fdIn; 
    data->fdOut=fdOut;
    data->fdToMaster=fdToMaster;

    data->fdFromSubleft=0;
    data->fdFromSubright=0;
    data->fdToSubleft=0;
    data->fdToSubright=0;
}


/************************************************************************
 * Stop 
 ************************************************************************/
void stopAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre stop\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si il y a un worker gauche mais pas de droit 
    if ((data->fdToSubleft!=0 && data->fdToSubleft!=0) && (data->fdFromSubright==0 && data->fdToSubright==0)){
      //envoi de l'ordre stop au worker gauche
      int orderToSend = MW_ORDER_STOP;
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre stop au worker gauche");  
      wait(NULL);
    }
    //sinon si il y a un worker droit mais pas de gauche
    else if ((data->fdToSubleft==0 && data->fdToSubleft==0) && (data->fdFromSubright!=0 && data->fdToSubright!=0)) {
      //envoi de l'ordre stop au worker droit
      int orderToSend = MW_ORDER_STOP;
      int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre stop au worker droit");  
      wait(NULL);
    }
    //sinon si il y a un work droit et gauche on envoie l'ordre stop au deux 
    else if ((data->fdToSubleft!=0 && data->fdToSubleft!=0) && (data->fdFromSubright!=0 && data->fdToSubright!=0)) {
      int orderToSend = MW_ORDER_STOP;
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre stop au worker gauche");  

      retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre stop au worker droit");  

      //on attend la fin des workers 
      wait(NULL);
      wait(NULL);
    }
}


/************************************************************************
 * Combien d'éléments
 ************************************************************************/
static void howManyAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre how many\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //cas où les fils n'existent pas
    if((data->fdToSubleft==0 && data->fdFromSubleft==0) && (data->fdToSubright==0 && data->fdFromSubright==0)){
      int receiptToSend = MW_ANSWER_HOW_MANY; 
      int retw = write(data->fdOut, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi de la cardinalité du worker
      int nb = data->nbOfElement; 
      retw = write(data->fdOut, &nb, sizeof(int));
      myassert(retw != -1, "echec envoi nombre élément");

      int nbDistinct = 1; 
      retw = write(data->fdOut, &nbDistinct, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon il y a au moins un fils
    else {
      //on initialise nos resultats 
      int nbTotal1=0;
      int nbTotal2=0;
      int nbDistinct1=0;
      int nbDistinct2=0;
      //si il y a un worker gauche 
      if((data->fdToSubleft!=0 && data->fdFromSubleft!=0)){

        //envoi de l'ordre au worker gauche 
        int orderToSend = MW_ORDER_HOW_MANY ; 
        int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
        myassert(retw != -1, "echec envoi ordre au worker gauche");    

        //reception de l'accusé de reception du worker gauche
        int receipt ;
        int retr = read(data->fdFromSubleft, &receipt, sizeof(int));
        myassert(retr != 0, "echec lecture accusé de reception");

        //reception du résultat envoyé par le worker gauche 
        retr = read(data->fdFromSubleft, &nbTotal1, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");

        //reception du résultat envoyé par le worker gauche 
        retr = read(data->fdFromSubleft, &nbDistinct1, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");
      }
      //si il y a un worker droit 
      if((data->fdToSubright!=0 && data->fdFromSubright!=0)){
        //envoi de l'ordre au worker droit 
        int orderToSend = MW_ORDER_HOW_MANY ; 
        int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
        myassert(retw != -1, "echec envoi ordre au worker gauche");    

        //reception de l'accusé de reception du worker droit
        int receipt ;
        int retr = read(data->fdFromSubright, &receipt, sizeof(int));
        myassert(retr != 0, "echec lecture accusé de reception");

        //reception du résultat envoyé par le worker droit 
        retr = read(data->fdFromSubright, &nbTotal2, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");

        //reception du résultat envoyé par le worker droit 
        retr = read(data->fdFromSubright, &nbDistinct2, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");
      }

      //envoi de l'accusé de reception au père 
      int receiptToSend = MW_ANSWER_HOW_MANY ; 
      int retw = write(data->fdOut, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");    

      //envoi des sommes des resultats des sous workers et la valeur du worker courant 

      int nbTotal = data->nbOfElement + nbTotal1 + nbTotal2; 
      retw = write(data->fdOut, &nbTotal, sizeof(int));
      myassert(retw != -1, "echec envoi cardinalité");

      int nbDistinct = 1 + nbDistinct1 + nbDistinct2; 
      retw = write(data->fdOut, &nbDistinct, sizeof(int));
      myassert(retw != -1, "echec envoi cardinalité");
    }
}


/************************************************************************
 * Minimum
 ************************************************************************/
static void minimumAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre minimum\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si le fils gauche n'existe pas (on est sur le minimum)
    if (data->fdToSubleft==0 && data->fdFromSubleft==0){
      //envoi de l'accusé de réception au master
      int receiptToSend = MW_ANSWER_MINIMUM;
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi de l'élément du worker courant au master
      float elementToSend = data->element;
      retw = write(data->fdToMaster, &elementToSend, sizeof(float));
      myassert(retw != -1, "echec envoi element");
    }
    //sinon (si le minimum n'est pas atteint)
    else {
      //envoi au worker gauche de l'ordre minimum
      int orderToSend = MW_ORDER_MINIMUM;
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker gauche"); 
    }
}


/************************************************************************
 * Maximum
 ************************************************************************/
static void maximumAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre maximum\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si le fils droit n'existe pas (on est sur le maximum)
    if (data->fdToSubright==0 && data->fdFromSubright==0){
      //envoi de l'accusé de réception au master
      int receiptToSend = MW_ANSWER_MAXIMUM;
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi de l'élément du worker courant au master
      float elementToSend = data->element;
      retw = write(data->fdToMaster, &elementToSend, sizeof(float));
      myassert(retw != -1, "echec envoi element");
    }
    //sinon (si le maximum n'est pas atteint)
    else {
      //envoi au worker droit de l'ordre maximum
      int orderToSend = MW_ORDER_MAXIMUM;
      int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker gauche"); 
    }
}


/************************************************************************
 * Existence
 ************************************************************************/
static void existAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre exist\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    // - recevoir l'élément à tester en provenance du père
    float elementReceived;
    int retr = read(data->fdIn, &elementReceived, sizeof(float));
    myassert(retr != 0, "echec lecture elt float");

    // - si élément courant == élément à tester
    if(elementReceived == data->element){
      //envoyer au master l'accusé de réception de réussite (cf. master_worker.h)
      int receiptToSend = MW_ANSWER_EXIST_YES; 
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoyer cardinalité de l'élément courant au master
      int quantity = data->nbOfElement; ;
      retw = write(data->fdToMaster, &quantity, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon si (elt à tester < elt courant) et (pas de fils gauche)
    else if ((elementReceived < data->element) && ((data->fdFromSubleft==0) && (data->fdToSubleft==0))){
      //envoi au master de l'accusé de réception d'echec
      int receiptToSend = MW_ANSWER_EXIST_NO; 
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon si (elt à tester > elt courant) et (pas de fils droit)
    else if ((elementReceived > data->element) && ((data->fdFromSubright==0) && (data->fdToSubright==0))){
      //envoi au master de l'accusé de réception d'echec
      int receiptToSend = MW_ANSWER_EXIST_NO; 
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon si (elt à insérer < elt courant)
    else if(elementReceived < data->element){
      int orderToSend = MW_ORDER_EXIST ; 
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker gauche");    

      retw = write(data->fdToSubleft, &elementReceived, sizeof(float));
      myassert(retw != -1, "echec envoi element");
    }
    //sinon (elt à insérer > elt courant)
    else{
      int orderToSend = MW_ORDER_EXIST ; 
      int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker gauche");    

      retw = write(data->fdToSubright, &elementReceived, sizeof(float));
      myassert(retw != -1, "echec envoi element");
    }
}


/************************************************************************
 * Somme
 ************************************************************************/
static void sumAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre sum\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

     //cas où les fils n'existent pas
    if((data->fdToSubleft==0 && data->fdFromSubleft==0) && (data->fdToSubright==0 && data->fdFromSubright==0)){
      //envoyer au père l'accusé de réception
      int receiptToSend = MW_ANSWER_SUM; 
      int retw = write(data->fdOut, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoyer la valeur du worker
      float element = (data->element) * (data->nbOfElement); 
      retw = write(data->fdOut, &element, sizeof(float));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon il y a au moins un fils
    else {
      //on initialise nos resultats 
      float result1=0;
      float result2=0;
      //si il y a un worker gauche 
      if((data->fdToSubleft!=0 && data->fdFromSubleft!=0)){

        //envoi de l'ordre au worker gauche 
        int orderToSend = MW_ORDER_SUM ; 
        int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
        myassert(retw != -1, "echec envoi ordre au worker gauche");    

        //reception de l'accusé de reception du worker gauche
        int receipt ;
        int retr = read(data->fdFromSubleft, &receipt, sizeof(int));
        myassert(retr != 0, "echec lecture accusé de reception");

        //reception du résultat envoyé par le worker gauche 
        retr = read(data->fdFromSubleft, &result1, sizeof(float));
        myassert(retr != 0, "echec lecture somme");

      }
      //si il y a un worker droit 
      if((data->fdToSubright!=0 && data->fdFromSubright!=0)){
        //envoi de l'ordre au worker droit 
        int orderToSend = MW_ORDER_SUM ; 
        int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
        myassert(retw != -1, "echec envoi ordre au worker gauche");    

        //reception de l'accusé de reception du worker droit
        int receipt ;
        int retr = read(data->fdFromSubright, &receipt, sizeof(int));
        myassert(retr != 0, "echec lecture accusé de reception");

        //reception du résultat envoyé par le worker droit 
        retr = read(data->fdFromSubright, &result2, sizeof(float));
        myassert(retr != 0, "echec lecture somme");
      }

      //envoi de l'accusé de reception au père 
      int receiptToSend = MW_ANSWER_SUM ; 
      int retw = write(data->fdOut, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");    

      //envoi de la somme des resultats des sous workers et la valeur du worker courant 
      float somme = ((data->element)*(data->nbOfElement))+ result1 + result2; 
      retw = write(data->fdOut, &somme, sizeof(float));
      myassert(retw != -1, "echec envoi somme");
    }
}


/************************************************************************
 * Insertion d'un nouvel élément
 ************************************************************************/
static void insertAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre insert\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    // - reception de l'élément à insérer en provenance du père
    float elementReceived;
    int retr = read(data->fdIn, &elementReceived, sizeof(float));
    myassert(retr != 0, "echec lecture accusé de reception");

    //printf("mtn je compare %f == %f ... c'est %d\n", elementReceived, data->element, elementReceived == data->element);
   
    //printf("hello im a worker and i received this element to insert %f ! \n", elementReceived);
  
    //si élément courant == élément à tester
    if (elementReceived == data->element){
      //on incrémente la cardinalité courante
      (data->nbOfElement)++; 

      //printf("I received element %f so now my nbOfElements is %d\n", elementReceived, data->nbOfElement);

      //envoie au master l'accusé de réception 
      int receiptToSend = MW_ANSWER_INSERT; ;
      int retw = write(data->fdToMaster, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }

    //sinon si (elt à tester < elt courant) et (pas de fils gauche)
    else if ((elementReceived < data->element) && ((data->fdFromSubleft==0) && (data->fdToSubleft==0))){

      //création des tubes anonymes 
      int fdsWorkertoSubleft[2];
      pipe(fdsWorkertoSubleft); 
      int fdsSubleftToWorker[2];
      pipe(fdsSubleftToWorker);

      //on fork le worker 
      int f1 = fork();

      //si on est dans le fils 
      if (f1==0){
        //on ferme les extrémités inutiles
        close(fdsSubleftToWorker[0]);
        close(fdsWorkertoSubleft[1]);

        //on convertit nos arguments en string
        char myEltString[50];
        sprintf(myEltString,"%f",elementReceived);

        char fdInString[50];
        sprintf(fdInString,"%d",fdsWorkertoSubleft[0]);

        char fdOutString[50];
        sprintf(fdOutString,"%d",fdsSubleftToWorker[1]);

        char fdtoMasterString[50];
        sprintf(fdtoMasterString, "%d", data->fdToMaster);

        // - crée un nouveau worker à "gauche" avec l'élément reçu
        execl("worker", "./worker", myEltString, fdInString, fdOutString, fdtoMasterString, NULL); 
      }
      //si on est dans le worker pere
      else{
         //on ferme les extrémités inutiles
        close(fdsSubleftToWorker[1]);
        close(fdsWorkertoSubleft[0]);

        //on renseigne les file descriptors permettant de communiquer avec le worker à gauche 
        data->fdFromSubleft = fdsSubleftToWorker[0];
        data->fdToSubleft = fdsWorkertoSubleft[1];
      }
    }
     //sinon si (elt à tester > elt courant) et (pas de fils droit)
    else if ((elementReceived > data->element) && ((data->fdFromSubright==0) && (data->fdToSubright==0))){

      //création des tubes anonymes 
      int fdsWorkertoSubright[2];
      pipe(fdsWorkertoSubright); 
      int fdsSubrightToWorker[2];
      pipe(fdsSubrightToWorker);

      //on fork le worker 
      int f2 = fork();

      //si on est dans le fils 
      if (f2==0){
        //on ferme les extrémités inutiles
        close(fdsSubrightToWorker[0]);
        close(fdsWorkertoSubright[1]);

        //on convertit nos arguments en string
        char myEltString[50];
        sprintf(myEltString,"%f",elementReceived);

        char fdInString[50];
        sprintf(fdInString,"%d",fdsWorkertoSubright[0]);

        char fdOutString[50];
        sprintf(fdOutString,"%d",fdsSubrightToWorker[1]);

        char fdtoMasterString[50];
        sprintf(fdtoMasterString, "%d", data->fdToMaster);

        // - crée un nouveau worker à "droite" avec l'élément reçu
        execl("worker", "./worker", myEltString, fdInString, fdOutString, fdtoMasterString, NULL); 
      }
      //si on est dans le worker pere
      else{
         //on ferme les extrémités inutiles
        close(fdsSubrightToWorker[1]);
        close(fdsWorkertoSubright[0]);

        //on renseigne les file descriptors permettant de communiquer avec le worker à droite 
        data->fdFromSubright = fdsSubrightToWorker[0];
        data->fdToSubright = fdsWorkertoSubright[1];
      }
    }
    //sinon si (elt à insérer < elt courant)
    else if(elementReceived < data->element){
      //envoi de l'ordre insert au worker gauche 
      int orderToSend = MW_ORDER_INSERT;
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi order au worker");

      //envoi de l'élément à insérer au worker gauche 
      retw = write(data->fdToSubleft, &elementReceived, sizeof(float));
      myassert(retw != -1, "echec envoi element au worker");
    }
    //sinon (donc elt à insérer > elt courant)
    else{
      //envoi de l'ordre insert au worker droit 
      int orderToSend = MW_ORDER_INSERT;
      int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi order au worker");

      //envoi de l'élément à insérer au worker droit 
      retw = write(data->fdToSubright, &elementReceived, sizeof(float));
      myassert(retw != -1, "echec envoi element au worker");
    }
}


/************************************************************************
 * Affichage
 ************************************************************************/
static void printAction(Data *data)
{
    TRACE3("    [worker (%d, %d) {%g}] : ordre print\n", getpid(), getppid(), data->element);
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si le worker gauche existe
    if (data->fdToSubleft!=0 && data->fdFromSubleft!=0){
      //envoi de l'ordre au worker gauche
      int orderToSend = MW_ORDER_PRINT;
      int retw = write(data->fdToSubleft, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi order au worker");

      //reception de l'accusé de reception du worker gauche 
      int receiptReceived;
      int retr = read(data->fdFromSubleft, &receiptReceived, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");
    }

    //si le worker droit existe
    if (data->fdToSubright!=0 && data->fdFromSubright!=0){
      //envoi de l'ordre au worker droit
      int orderToSend = MW_ORDER_PRINT;
      int retw = write(data->fdToSubright, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi order au worker");

      //reception de l'accusé de reception du worker droit 
      int receiptReceived;
      int retr = read(data->fdFromSubright, &receiptReceived, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");
    }

    //Affichage du worker
    TRACE2("[%f, %d]\n", data->element, data->nbOfElement);

    //envoi de l'accusé de reception au père 
    int receiptToSend = MW_ANSWER_PRINT; ;
    int retw = write(data->fdOut, &receiptToSend, sizeof(int));
    myassert(retw != -1, "echec envoi accusé de reception");
}


/************************************************************************
 * Boucle principale de traitement
 ************************************************************************/
void loop(Data *data)
{
    bool end = false;

    while (! end)
    {
        int orderReceived;

        int retr = read(data->fdIn, &orderReceived, sizeof(int));
        myassert(retr != 0, "error loop worker.c : echec lecture order");

        //printf("WORKER : lecture de l'ordre ok !\n");

        //printf("WORKER : J'ai recu l'ordre suivant : %d ! \n", orderReceived);

        switch(orderReceived)
        {
          case MW_ORDER_STOP:
            stopAction(data);
            end = true;
            break;
          case MW_ORDER_HOW_MANY:
            howManyAction(data);
            break;
          case MW_ORDER_MINIMUM:
            minimumAction(data);
            break;
          case MW_ORDER_MAXIMUM:
            maximumAction(data);
            break;
          case MW_ORDER_EXIST:
            existAction(data);
            break;
          case MW_ORDER_SUM:
            sumAction(data);
            break;
          case MW_ORDER_INSERT:
            insertAction(data);
            break;
          case MW_ORDER_PRINT:
            printAction(data);
            break;
          default:
            myassert(false, "ordre inconnu");
            exit(EXIT_FAILURE);
            break;
        }

        TRACE3("    [worker (%d, %d) {%g}] : fin ordre\n", getpid(), getppid(), data->element);
    }
}


/************************************************************************
 * Programme principal
 ************************************************************************/

int main(int argc, char * argv[])
{
    Data data;
    parseArgs(argc, argv, &data);
    TRACE3("    [worker (%d, %d) {%g}] : début worker\n", getpid(), getppid(), data.element);

    //envoi au master l'accusé de réception d'insertion 
    int receiptToSend = MW_ANSWER_INSERT; ;
    int retw = write(data.fdToMaster, &receiptToSend, sizeof(int));
    myassert(retw != -1, "echec envoi accusé de reception");

    //note : en effet si je suis créé c'est qu'on vient d'insérer un élément : moi

    loop(&data);

    //fermer les tubes
    int ret1 = close(data.fdIn);
    myassert(ret1 == 0, "echec fermeture pipe In");

    int ret2 = close(data.fdOut);
    myassert(ret2 == 0, "echec fermeture pipe Out");

    int ret3 = close(data.fdToMaster);
    myassert(ret3 == 0, "echec fermeture pipe toMaster");

    TRACE3("    [worker (%d, %d) {%g}] : fin worker\n", getpid(), getppid(), data.element);
    return EXIT_SUCCESS;
}
