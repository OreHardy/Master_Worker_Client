#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sem.h>

#include "utils.h"
#include "myassert.h"

#include "client_master.h"
#include "master_worker.h"

/************************************************************************
 * Données persistantes d'un master
 ************************************************************************/
typedef struct
{
    // communication avec le client
    int fdClientToMaster;
    int fdMasterToClient;
    // données internes
    bool hasChild;
    bool isInInsertMany;
    float elementInsertMany;
    // communication avec le premier worker (double tubes)
    int fdWorker1ToMaster;
    int fdMasterToWorker1;
    // communication en provenance de tous les workers (un seul tube en lecture)
    int fdAnyWorkerToMaster;
} Data;


/************************************************************************
 * Usage et analyse des arguments passés en ligne de commande
 ************************************************************************/
static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usage : %s\n", exeName);
    if (message != NULL)
        fprintf(stderr, "message : %s\n", message);
    exit(EXIT_FAILURE);
}


/************************************************************************
 * initialisation complète
 ************************************************************************/
void init(Data *data)
{
    myassert(data != NULL, "il faut l'environnement d'exécution");

    data->hasChild = false ;
    data->isInInsertMany = false;
    data->elementInsertMany = 0;
}


/************************************************************************
 * fin du master
 ************************************************************************/
void orderStop(Data *data)
{
    TRACE0("[master] ordre stop\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //cas pas de premier worker
    if (data->hasChild){
      //envoi de l'ordre d'arrêt au premier worker
      int orderToSend = MW_ORDER_STOP;
      int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre stop au worker 1");  
      //attente de la fin du worker1 
      wait(NULL);
    }
    // - envoi de l'accusé de réception au client 
    int receiptToSend = MW_ORDER_STOP;
    int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
    myassert(retw != -1, "echec envoi accusé de receptin stop au client");  

}


/************************************************************************
 * quel est la cardinalité de l'ensemble
 ************************************************************************/
void orderHowMany(Data *data)
{
    TRACE0("[master] ordre how many\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //initialisation nos variables
    int nbTotal = 0 ;
    int nbDistinct = 0; 
    int receiptToSend ;

    //cas aucun worker
    if (!data->hasChild){

      //envoi de l'accusé de reception au client 
      receiptToSend = CM_ANSWER_HOW_MANY_OK ;
      int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi des deux cardinalités aux clients ici (0)
      retw = write(data->fdMasterToClient, &nbTotal, sizeof(int));
      myassert(retw != -1, "echec envoi cardinalité");

      retw = write(data->fdMasterToClient, &nbDistinct, sizeof(int));
      myassert(retw != -1, "echec envoi cardinalité");
    }
    //si il y au moins un worker
    else { 
      //envoi l'ordre au worker
      int order = MW_ORDER_HOW_MANY ;
      int retw = write(data->fdMasterToWorker1, &order, sizeof(int));
      myassert(retw != -1, "echec envoi order");

      //recepetion de l'accusé de reception
      int receipt ;
      int retr = read(data->fdWorker1ToMaster, &receipt, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");

      //reception des deux cardinalités 
      retr = read(data->fdWorker1ToMaster, &nbTotal, sizeof(int));
      myassert(retr != 0, "echec lecture cardinalité");

      retr = read(data->fdWorker1ToMaster, &nbDistinct, sizeof(int));
      myassert(retr != 0, "echec lecture cardinalité");

      //envoi de l'accusé de reception au client
      retw = write(data->fdMasterToClient, &receipt, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
      
      //envoi de nos deux cardinalités au client
      retw = write(data->fdMasterToClient, &nbTotal, sizeof(int));
      myassert(retw != -1, "echec envoi somme");

      retw = write(data->fdMasterToClient, &nbDistinct, sizeof(int));
      myassert(retw != -1, "echec envoi somme");
    }
}


/************************************************************************
 * quel est la minimum de l'ensemble
 ************************************************************************/
void orderMinimum(Data *data)
{
    TRACE0("[master] ordre minimum\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si ensemble vide (pas de premier worker)
    if (!data->hasChild){
      //envoi de l'accusé de reception prévu au client
      int receiptSent = CM_ANSWER_MINIMUM_EMPTY;
      int retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon
    else{
      //envoi au premier worker de l'ordre minimum 
      int orderToSend = MW_ORDER_MINIMUM;
      int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre"); 

      //reception de l'accusé de réception venant du worker concerné
      int receipt ;
      int retr = read(data->fdAnyWorkerToMaster, &receipt, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");

      //reception du résultat venant du worker concerné
      float result; 
      retr = read(data->fdAnyWorkerToMaster, &result, sizeof(float));
      myassert(retr != 0, "echec lecture element");

      //envoi de l'accusé de réception au client
      retw = write(data->fdMasterToClient, &receipt, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception"); 

      //envoi du résultat au client
      retw = write(data->fdMasterToClient, &result, sizeof(float));
      myassert(retw != -1, "echec envoi element"); 
    }
}


/************************************************************************
 * quel est la maximum de l'ensemble
 ************************************************************************/
void orderMaximum(Data *data)
{
    TRACE0("[master] ordre maximum\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //si ensemble vide (pas de premier worker)
    if (!data->hasChild){
      //envoi de l'accusé de reception prévu au client
      int receiptSent = CM_ANSWER_MAXIMUM_EMPTY;
      int retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    //sinon
    else{
      //envoi au premier worker de l'ordre maximim 
      int orderToSend = MW_ORDER_MAXIMUM;
      int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre"); 

      //reception de l'accusé de réception venant du worker concerné
      int receipt ;
      int retr = read(data->fdAnyWorkerToMaster, &receipt, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception") ;

      //reception du résultat venant du worker concerné
      float result; 
      retr = read(data->fdAnyWorkerToMaster, &result, sizeof(float));
      myassert(retr != 0, "echec lecture element");

      //envoi de l'accusé de réception au client
      retw = write(data->fdMasterToClient, &receipt, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception"); 

      //envoi du résultat au client
      retw = write(data->fdMasterToClient, &result, sizeof(float));
      myassert(retw != -1, "echec envoi element"); 
    }
}


/************************************************************************
 * test d'existence
 ************************************************************************/
void orderExist(Data *data)
{
    TRACE0("[master] ordre existence\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //reception de l'élément à insérer en provenance du client
    float myElt;
    int retr = read(data->fdClientToMaster, &myElt, sizeof(float));
    myassert(retr != 0, "echec lecture elt float");

    //si pas de premier worker
    if(!data->hasChild){
      //envoi de l'accusé de reception prévu
      int receiptSent = CM_ANSWER_EXIST_NO;
      int retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
      myassert(retw != -1, "error orderMaximum : echec envoi accusé de reception");
    }
    //si il y a au moins un worker 
    else{
      //envoi de l'ordre au premier worker 
      int orderToSend = MW_ORDER_EXIST ; 
      int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker 1");    

      //envoi de l'élément à vérifier
      retw = write(data->fdMasterToWorker1, &myElt, sizeof(float));
      myassert(retw != -1, "echec envoi element");

      //reception de la réponse du worker concerné 
      int receiptReceived;
      int retr = read(data->fdAnyWorkerToMaster, &receiptReceived, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");
      //printf("MASTER : j'ai recu la reponse : %d\n", receiptReceived);

      //si l'élément n'existe pas 
      if (receiptReceived == MW_ANSWER_EXIST_NO){
        //envoi de l'accusé de reception au client
        int receiptToSend = receiptReceived+1 ; //conversion pour la réponse du worker et celle du client
        int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
        myassert(retw != -1, "echec envoi accusé de reception");
      }
      //si l'élément existe 
      else {
          //reception de la cardinalité de l'élément 
          int quantity;
          int retr = read(data->fdAnyWorkerToMaster, &quantity, sizeof(int));
          myassert(retr != 0, "echec lecture quantite recue");

          //envoi de l'accusé de reception au client
          int receiptToSend = receiptReceived-1 ; //conversion pour la réponse du worker et celle du client
          int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
          myassert(retw != -1, "echec envoi accusé de reception");

          //envoi de la cardinalité au client 
          retw = write(data->fdMasterToClient, &quantity, sizeof(int));
          myassert(retw != -1, "echec envoi quantite");

      }

    }

}

/************************************************************************
 * somme
 ************************************************************************/
void orderSum(Data *data)
{
    TRACE0("[master] ordre somme\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");
    float resultToSend = 0 ;
    int receiptToSend;
    
    //cas pas de premier worker 
    if (!data->hasChild){
      //envoi de l'accusé de reception au client 
      receiptToSend = CM_ANSWER_SUM_OK ;
      int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi du résultat de la somme au client (ici 0)
      retw = write(data->fdMasterToClient, &resultToSend, sizeof(float));
      myassert(retw != -1, "echec envoi somme");
    }
    //si il existe au moins un worker 
    else {
      //envoi de l'ordre vers le premier worker
      int order = MW_ORDER_SUM ;
      int retw = write(data->fdMasterToWorker1, &order, sizeof(int));
      myassert(retw != -1, "echec envoi order");

      //reception de la réponse du premier worker
      int receipt ;
      int retr = read(data->fdWorker1ToMaster, &receipt, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");

      //reception du résultat de la somme venant du premier worker
      retr = read(data->fdWorker1ToMaster, &resultToSend, sizeof(float));
      myassert(retr != 0, "echec lecture somme");

      //envoi de l'accusé de reception au client
      retw = write(data->fdMasterToClient, &receipt, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");

      //envoi du résultat de la somme au client
      retw = write(data->fdMasterToClient, &resultToSend, sizeof(float));
      myassert(retw != -1, "echec envoi somme");
    }
}

/************************************************************************
 * insertion d'un élément
 ************************************************************************/
void orderInsert(Data *data)
{
    TRACE0("[master] ordre insertion\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    float myElt;
    //si on est dans un cas insertmany on récupère l'élément à inserer depuis data 
    if (data->isInInsertMany){
      myElt = data->elementInsertMany;
    }
    //sinon on receptionne l'élément depuis le client
    else {
      // - reception de l'élément à insérer en provenance du client
      int retr = read(data->fdClientToMaster, &myElt, sizeof(float));
      myassert(retr != 0, "echec lecture elt float");
    }

    // - si pas de premier worker
    if(!data->hasChild){

      //création des tubes anonymes
      int fdsMastertoWorker1[2];
      pipe(fdsMastertoWorker1); 
      int fdsWorker1toMaster[2];
      pipe(fdsWorker1toMaster);
      int fdsAnyWorkertoMaster[2];
      pipe(fdsAnyWorkertoMaster);

      //on fork le master
      int f1 = fork();
      //si on est dans le fils, le fils devient alors le premier worker 
      if(f1 == 0){

        //on ferme les extrémités inutiles 
        close(fdsMastertoWorker1[1]);
        close(fdsWorker1toMaster[0]);
        close(fdsAnyWorkertoMaster[0]);

        //on convertit nos arguments en string
        char myEltString[50];
        sprintf(myEltString,"%f",myElt);

        char fdInString[50];
        sprintf(fdInString,"%d",fdsMastertoWorker1[0]);

        char fdOutString[50];
        sprintf(fdOutString,"%d",fdsWorker1toMaster[1]);

        char fdtoMasterString[50];
        sprintf(fdtoMasterString, "%d", fdsAnyWorkertoMaster[1]);

        // - crée le premier worker avec l'élément reçu du client
        execl("worker", "./worker", myEltString, fdInString, fdOutString, fdtoMasterString, NULL); 
      }
      // alors on est dans le père 
      else{
        
        //on ferme les extrémités inutiles
        close(fdsMastertoWorker1[0]);
        close(fdsWorker1toMaster[1]);
        close(fdsAnyWorkertoMaster[1]);

        //on renseigne les file descriptors permettant de communiquer avec le worker1 et celui du retour des workers
        data->fdMasterToWorker1=fdsMastertoWorker1[1];
        data->fdWorker1ToMaster=fdsWorker1toMaster[0];
        data->fdAnyWorkerToMaster=fdsAnyWorkertoMaster[0];

        //et maintenant on a un premier worker (enfant)
        data->hasChild = true ;
      }
    }
    // si on a deja un premier worker (enfant)
    else {
    //envoie au premier worker de l'ordre insertion 
    int orderToSend = MW_ORDER_INSERT ; 
    int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
    myassert(retw != -1, "echec envoi ordre au worker 1");    

    //envoie au premier worker l'élément à insérer
    retw = write(data->fdMasterToWorker1, &myElt, sizeof(float));
    myassert(retw != -1, "echec envoi element");
    }

    //si on est pas dans un cas d'insert many on procède normalement
    if (!data->isInInsertMany){
    //reception de l'accusé de réception venant du worker concerné
    int receiptReceived;
    int retr = read(data->fdAnyWorkerToMaster, &receiptReceived, sizeof(int));
    myassert(retr != 0, "echec lecture accusé de reception");

    //envoi de l'accusé de réception au client
    int receiptToSend = receiptReceived ; 
    int retw = write(data->fdMasterToClient, &receiptToSend, sizeof(int));
    myassert(retw != -1, "echec envoi accusé de reception");
    }
    //si on est dans un cas d'insert many
    else {
      data->isInInsertMany = false;
    }
}


/************************************************************************
 * insertion d'un tableau d'éléments
 ************************************************************************/
void orderInsertMany(Data *data)
{
    TRACE0("[master] ordre insertion tableau\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //reception de la taille du tableau d'éléments à insérer en provenance du client
    int size; 
    int retr = read(data->fdClientToMaster, &size, sizeof(int));
    myassert(retr != 0, "echec lecture taille du tableau");

    //printf(" la taille du tableau est %d \n", size);

    //reception du tableau d'éléments à insérer en provenance du client
    float tab[size];
    retr = read(data->fdClientToMaster, &tab, size * sizeof(float));
    myassert(retr != 0, "echec lecture tableau");

    //on insère chaque élément 
    for (int i = 0; i<size; i++){
      //on active notre variable qui indique qu'on est dans un cas InsertMany
      data->isInInsertMany=true;
      //on passe dans data l'element à inserer
      data->elementInsertMany=tab[i];

      //on envoie l'ordre d'insertion 
      orderInsert(data);

      //on intercepte l'accusé de reception prévu par l'ordre insert
      int receiptReceived;
      retr = read(data->fdAnyWorkerToMaster, &receiptReceived, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");
    }

    //on envoie l'accusé de reception au client 
    int receiptSent = CM_ANSWER_INSERT_MANY_OK;
    int retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
    myassert(retw != -1, "echec envoi accusé de reception");
    
    //on reinitialise nos variables 
    data->elementInsertMany=false; 
    data->elementInsertMany=0;

}


/************************************************************************
 * affichage ordonné
 ************************************************************************/
void orderPrint(Data *data)
{
    TRACE0("[master] ordre affichage\n");
    myassert(data != NULL, "il faut l'environnement d'exécution");

    //cas ensemble vide
    if (!data->hasChild){
      TRACE0("pas de premier worker\n");
      int receiptSent = CM_ANSWER_PRINT_OK;
      int retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
    else{
      //envoi au premier worker de l'ordre print
      int orderToSend = MW_ORDER_PRINT ; 
      int retw = write(data->fdMasterToWorker1, &orderToSend, sizeof(int));
      myassert(retw != -1, "echec envoi ordre au worker 1");    

      //reception de l'accusé de réception venant du premier worker
      int receiptReceived;
      int retr = read(data->fdWorker1ToMaster, &receiptReceived, sizeof(int));
      myassert(retr != 0, "echec lecture accusé de reception");

      //envoi de l'accusé de reception vers le client avec la conversion vers le bon ordre 
      int receiptSent = receiptReceived + 10;
      retw = write(data->fdMasterToClient, &receiptSent, sizeof(int));
      myassert(retw != -1, "echec envoi accusé de reception");
    }
}


/************************************************************************
 * boucle principale de communication avec le client
 ************************************************************************/
void loop(Data *data)
{
    bool end = false;

    init(data);

    while (! end)
    {
        // - ouverture des tubes nommés 
        data->fdClientToMaster = open("pipe1", O_RDONLY);
        myassert(data->fdClientToMaster != -1, "echec ouverture pipe ClientToMaster");
        //printf("MASTER : ouverture lecture ClienToMaster ok !\n");

        data->fdMasterToClient = open("pipe2", O_WRONLY);
        myassert(data->fdClientToMaster != -1, "echec ouverture pipe MasterToClient");
        //printf("MASTER : ouverture écriture MasterToClient ok !\n");

        // - reception de l'ordre 
        int orderReceived; 
        int retr = read(data->fdClientToMaster, &orderReceived, sizeof(int));
        myassert(retr != 0, "echec lecture order");
        //printf("MASTER : lecture de l'ordre ok !\n");

        //printf("MASTER : J'ai recu l'ordre suivant : %d ! \n", orderReceived);

        //printf("GetChar bloquant pour ajouter des clients : \n");
        //char c = getchar(); 

        switch(orderReceived)
        {
          case CM_ORDER_STOP:
            orderStop(data);
            end = true;
            break;
          case CM_ORDER_HOW_MANY:
            orderHowMany(data);
            break;
          case CM_ORDER_MINIMUM:
            orderMinimum(data);
            break;
          case CM_ORDER_MAXIMUM:
            orderMaximum(data);
            break;
          case CM_ORDER_EXIST:
            orderExist(data);
            break;
          case CM_ORDER_SUM:
            orderSum(data);
            break;
          case CM_ORDER_INSERT:
            orderInsert(data);
            break;
          case CM_ORDER_INSERT_MANY:
            orderInsertMany(data);
            break;
          case CM_ORDER_PRINT:
            orderPrint(data);
            break;
          default:
            myassert(false, "ordre inconnu");
            exit(EXIT_FAILURE);
            break;
        }

        //attend la fin de la lecture de l'accusé de reception par le client  
        struct sembuf operationMoins = {0, -1, 0};

        int semId1 = semget(KEY1, 1, 0);
        myassert(semId1 != -1, "echec ouverture sema 1");

        int retsem1 = semop(semId1, &operationMoins, 1);
        myassert(retsem1 != -1, "erreur 'acheter' sémaphore");

        //fermeture des tubes nommés
        int ret1 = close(data->fdClientToMaster);
        myassert(ret1 == 0, "echec fermeture pipe ClientTomaster");

        int ret2 = close(data->fdMasterToClient);
        myassert(ret2 == 0, "echec fermeture pipe MasterToClient");

        TRACE0("[master] fin ordre\n");
    }
}


/************************************************************************
 * Fonction principale
 ************************************************************************/

int main(int argc, char * argv[])
{
    if (argc != 1)
        usage(argv[0], NULL);

    TRACE0("[master] début\n");

    Data data;

    // - création des sémaphores
    int semId1 = semget(KEY1, 1, IPC_CREAT | IPC_EXCL | 0641);
    myassert(semId1 != -1, "echec creation sema 1");

    int semId2 = semget(KEY2, 1, IPC_CREAT | IPC_EXCL | 0641);
    myassert(semId2 != -1, "echec creation sema 2");
    
    // - création des tubes nommés
    int ret1 = mkfifo("pipe1", 0644);
    myassert(ret1 == 0, "echec creation pipe MasterToClient");
    
    int ret2 = mkfifo("pipe2", 0644);
    myassert(ret2 == 0, "echec creation pipe ClientToMaster");

    TRACE0("  [master] semaphores and pipes created !\n");

    // - initialisation des semaphores 
    int ret3 = semctl(semId1, 0, SETVAL, 0);
    myassert(ret3 != -1, "initialisation sémaphore");

    int ret4 = semctl(semId2, 0, SETVAL, 1);
    myassert(ret3 != -1, "initialisation sémaphore");
        
    loop(&data);

    //destruction des tubes nommés
    ret1 = unlink("pipe1");
    myassert(ret1 == 0, "echec fermeture pipe MasterToClient");

    ret2 = unlink("pipe2");
    myassert(ret2 == 0, "echec fermeture pipe ClientToMaster");

    //destruction des des sémaphores
    ret3 = semctl(semId1, -1, IPC_RMID);
    myassert(ret3 != -1, "echec destruction sémaphore 1");

    ret4 = semctl(semId2, -1, IPC_RMID);
    myassert(ret4 != -1, "echec destruction sémaphore 2");

    TRACE0("  [master] semaphores and pipes destroyed !\n");

    TRACE0("[master] terminaison\n");
    return EXIT_SUCCESS;
}
