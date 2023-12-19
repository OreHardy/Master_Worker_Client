#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sem.h>

#include "utils.h"
#include "myassert.h"

#include "client_master.h"


/************************************************************************
 * chaines possibles pour le premier paramètre de la ligne de commande
 ************************************************************************/
#define TK_STOP        "stop"             // arrêter le master
#define TK_HOW_MANY    "howmany"          // combien d'éléments dans l'ensemble
#define TK_MINIMUM     "min"              // valeur minimale de l'ensemble
#define TK_MAXIMUM     "max"              // valeur maximale de l'ensemble
#define TK_EXIST       "exist"            // test d'existence d'un élément, et nombre d'exemplaires
#define TK_SUM         "sum"              // somme de tous les éléments
#define TK_INSERT      "insert"           // insertion d'un élément
#define TK_INSERT_MANY "insertmany"       // insertions de plusieurs éléments aléatoires
#define TK_PRINT       "print"            // debug : demande aux master/workers d'afficher les éléments
#define TK_LOCAL       "local"            // lancer un calcul local (sans master) en multi-thread


/************************************************************************
 * structure stockant les paramètres du client
 * - les infos pour communiquer avec le master
 * - les infos pour effectuer le travail (cf. ligne de commande)
 *   (note : une union permettrait d'optimiser la place mémoire)
 ************************************************************************/
typedef struct {
    // communication avec le master
    int fdMasterToClient;
    int fdClientToMaster;
    // infos pour le travail à faire (récupérées sur la ligne de commande)
    int order;     // ordre de l'utilisateur (cf. CM_ORDER_* dans client_master.h)
    float elt;     // pour CM_ORDER_EXIST, CM_ORDER_INSERT, CM_ORDER_LOCAL
    int nb;        // pour CM_ORDER_INSERT_MANY, CM_ORDER_LOCAL
    float min;     // pour CM_ORDER_INSERT_MANY, CM_ORDER_LOCAL
    float max;     // pour CM_ORDER_INSERT_MANY, CM_ORDER_LOCAL
    int nbThreads; // pour CM_ORDER_LOCAL
} Data;

/************************************************************************
 * Usage
 ************************************************************************/
static void usage(const char *exeName, const char *message)
{
    fprintf(stderr, "usages : %s <ordre> [[[<param1>] [<param2>] ...]]\n", exeName);
    fprintf(stderr, "   $ %s " TK_STOP "\n", exeName);
    fprintf(stderr, "          arrêt master\n");
    fprintf(stderr, "   $ %s " TK_HOW_MANY "\n", exeName);
    fprintf(stderr, "          combien d'éléments dans l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_MINIMUM "\n", exeName);
    fprintf(stderr, "          plus petite valeur de l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_MAXIMUM "\n", exeName);
    fprintf(stderr, "          plus grande valeur de l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_EXIST " <elt>\n", exeName);
    fprintf(stderr, "          l'élement <elt> est-il présent dans l'ensemble ?\n");
    fprintf(stderr, "   $ %s " TK_SUM "\n", exeName);
    fprintf(stderr, "           somme des éléments de l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_INSERT " <elt>\n", exeName);
    fprintf(stderr, "          ajout de l'élement <elt> dans l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_INSERT_MANY " <nb> <min> <max>\n", exeName);
    fprintf(stderr, "          ajout de <nb> élements (dans [<min>,<max>[) aléatoires dans l'ensemble\n");
    fprintf(stderr, "   $ %s " TK_PRINT "\n", exeName);
    fprintf(stderr, "          affichage trié (dans la console du master)\n");
    fprintf(stderr, "   $ %s " TK_LOCAL " <nbThreads> <elt> <nb> <min> <max>\n", exeName);
    fprintf(stderr, "          combien d'exemplaires de <elt> dans <nb> éléments (dans [<min>,<max>[)\n"
                    "          aléatoires avec <nbThreads> threads\n");

    if (message != NULL)
        fprintf(stderr, "message :\n    %s\n", message);

    exit(EXIT_FAILURE);
}


/************************************************************************
 * Analyse des arguments passés en ligne de commande
 ************************************************************************/
static void parseArgs(int argc, char * argv[], Data *data)
{
    data->order = CM_ORDER_NONE;

    if (argc == 1)
        usage(argv[0], "Il faut préciser une commande");

    // première vérification : la commande est-elle correcte ?
    if (strcmp(argv[1], TK_STOP) == 0)
        data->order = CM_ORDER_STOP;
    else if (strcmp(argv[1], TK_HOW_MANY) == 0)
        data->order = CM_ORDER_HOW_MANY;
    else if (strcmp(argv[1], TK_MINIMUM) == 0)
        data->order = CM_ORDER_MINIMUM;
    else if (strcmp(argv[1], TK_MAXIMUM) == 0)
        data->order = CM_ORDER_MAXIMUM;
    else if (strcmp(argv[1], TK_EXIST) == 0)
        data->order = CM_ORDER_EXIST;
    else if (strcmp(argv[1], TK_SUM) == 0)
        data->order = CM_ORDER_SUM;
    else if (strcmp(argv[1], TK_INSERT) == 0)
        data->order = CM_ORDER_INSERT;
    else if (strcmp(argv[1], TK_INSERT_MANY) == 0)
        data->order = CM_ORDER_INSERT_MANY;
    else if (strcmp(argv[1], TK_PRINT) == 0)
        data->order = CM_ORDER_PRINT;
    else if (strcmp(argv[1], TK_LOCAL) == 0)
        data->order = CM_ORDER_LOCAL;
    else
        usage(argv[0], "commande inconnue");

    // deuxième vérification : nombre de paramètres correct ?
    if ((data->order == CM_ORDER_STOP) && (argc != 2))
        usage(argv[0], TK_STOP " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_HOW_MANY) && (argc != 2))
        usage(argv[0], TK_HOW_MANY " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_MINIMUM) && (argc != 2))
        usage(argv[0], TK_MINIMUM " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_MAXIMUM) && (argc != 2))
        usage(argv[0], TK_MAXIMUM " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_EXIST) && (argc != 3))
        usage(argv[0], TK_EXIST " : il faut un et un seul argument après la commande");
    if ((data->order == CM_ORDER_SUM) && (argc != 2))
        usage(argv[0], TK_SUM " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_INSERT) && (argc != 3))
        usage(argv[0], TK_INSERT " : il faut un et un seul argument après la commande");
    if ((data->order == CM_ORDER_INSERT_MANY) && (argc != 5))
        usage(argv[0], TK_INSERT_MANY " : il faut 3 arguments après la commande");
    if ((data->order == CM_ORDER_PRINT) && (argc != 2))
        usage(argv[0], TK_PRINT " : il ne faut pas d'argument après la commande");
    if ((data->order == CM_ORDER_LOCAL) && (argc != 7))
        usage(argv[0], TK_LOCAL " : il faut 5 arguments après la commande");

    // extraction des arguments
    if (data->order == CM_ORDER_EXIST)
    {
        data->elt = strtof(argv[2], NULL);
    }
    else if (data->order == CM_ORDER_INSERT)
    {
        data->elt = strtof(argv[2], NULL);
    }
    else if (data->order == CM_ORDER_INSERT_MANY)
    {
        data->nb = strtol(argv[2], NULL, 10);
        data->min = strtof(argv[3], NULL);
        data->max = strtof(argv[4], NULL);
        if (data->nb < 1)
            usage(argv[0], TK_INSERT_MANY " : nb doit être strictement positif");
        if (data->max < data->min)
            usage(argv[0], TK_INSERT_MANY " : max ne doit pas être inférieur à min");
    }
    else if (data->order == CM_ORDER_LOCAL)
    {
        data->nbThreads = strtol(argv[2], NULL, 10);
        data->elt = strtof(argv[3], NULL);
        data->nb = strtol(argv[4], NULL, 10);
        data->min = strtof(argv[5], NULL);
        data->max = strtof(argv[6], NULL);
        if (data->nbThreads < 1)
            usage(argv[0], TK_LOCAL " : nbThreads doit être strictement positif");
        if (data->nb < 1)
            usage(argv[0], TK_LOCAL " : nb doit être strictement positif");
        if (data->max <= data->min)
            usage(argv[0], TK_LOCAL " : max ne doit être strictement supérieur à min");
    }
}

/************************************************************************
 * Partie multi-thread
 ************************************************************************/
//Une structure pour les arguments à passer à un thread (aucune variable globale autorisée)
typedef struct {
    int val;
    float* mytab;
    int *result;
    int debut ;
    int fin ;
    pthread_mutex_t myMutex ;
} threadData ;

void * thread_function(void * arg){

    threadData *data = (threadData*)arg; 
    for (int i = data->debut; i<=data->fin; i++){
        if(data->val==data->mytab[i]){
            pthread_mutex_lock(&data->myMutex);
            (*(data->result))++;
            pthread_mutex_unlock(&data->myMutex);
        }
    }
    free(arg); 
    return NULL;
}

void lauchThreads(const Data *data)
{
    //variables nécessaires
    int result = 0;
    float * tab = ut_generateTab(data->nb, data->min, data->max, 0);
    int pasTableau = data->nb-1/data->nbThreads; //-1 car on commence les indices a 0
    int resteTableau = data->nb-1%data->nbThreads;
    int val = data->elt;
    pthread_t pthreadTab[data->nbThreads]; //tableau d'identifiant de thread

    //création mutex
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    //Lancement des threads
    for(int i = 0 ; i < data->nbThreads ; i++){
        threadData* myData = malloc(sizeof(threadData));
        myData->val = val ;
        myData->mytab = tab ;
        myData->result = &result ;
        if(i == 0){
            myData->debut = 0 ; //si debut du tableau on commence a 0
        }
        else{
            myData->debut = pasTableau*i+1 ; //sinon on commance au dernier incice +1
        }
        if(i == data->nbThreads-1){
            myData->fin = pasTableau*(i+1)+resteTableau ; //si on est a la fin du tableau on prend le reste de la division
        }
        else {
            myData->fin = pasTableau*(i+1);
        }
        myData->myMutex = mutex ;
        int ret = pthread_create(&(pthreadTab[i]), NULL, thread_function, myData);
        myassert(ret == 0, "Client : erreur création thread");
    }

    //attente de la fin des threads
    for(int i = 0 ; i < data->nbThreads ; i++){
        int ret = pthread_join(pthreadTab[i],NULL);
        myassert(ret == 0, "Erreur client : fermeture des threads");
    }

    // résultat (result a été rempli par les threads)
    // affichage du tableau si pas trop gros
    if (data->nb <= 20)
    {
        printf("[");
        for (int i = 0; i < data->nb; i++)
        {
            if (i != 0)
                printf(" ");
            printf("%g", tab[i]);
        }
        printf("]\n");
    }
    // recherche linéaire pour vérifier
    int nbVerif = 0;
    for (int i = 0; i < data->nb; i++)
    {
        if (tab[i] == data->elt)
            nbVerif ++;
    }
    printf("Elément %g présent %d fois (%d attendu)\n", data->elt, result, nbVerif);
    if (result == nbVerif)
        printf("=> ok ! le résultat calculé par les threads est correct\n");
    else
        printf("=> PB ! le résultat calculé par les threads est incorrect\n");

    //libération des ressources
    int ret = pthread_mutex_destroy(&mutex);
    myassert(ret==0, "echec fermeture thread mutex");

    free(tab);
}


/************************************************************************
 * Partie communication avec le master
 ************************************************************************/
//envoi des données au master
void sendData(const Data *data)
{
    //envoi de l'ordre au master 
    int order = data->order;
    
    int retw = write(data->fdClientToMaster, &order, sizeof(int)); 
    myassert(retw != -1, "error sendData : echec envoi order");

    // envoi des paramètres supplémentaires au master (pour CM_ORDER_EXIST,
    // CM_ORDER_INSERT et CM_ORDER_INSERT_MANY)
    if(order == CM_ORDER_INSERT){
        float elt = data->elt;
        int retw = write(data->fdClientToMaster, &elt, sizeof(float)); 
        myassert(retw != -1, "echec envoi element");
    }
    else if(order == CM_ORDER_EXIST){
        float elt = data->elt;
        int retw = write(data->fdClientToMaster, &elt, sizeof(float)); 
        myassert(retw != -1, "echec envoi element");
    }
    else if(order == CM_ORDER_INSERT_MANY){
        int size = data->nb;
        int retw = write(data->fdClientToMaster, &size, sizeof(int)); 
        myassert(retw != -1, "echec envoi taille tableau");

        //ici soit on prend des variables aléatoires
        //ou on appelle notre fonction qui determine un tableau de valeur 
        //à partir d'un intervalle et d'un pas calculé à partir de min,
        //max et nb (VOIR UTILS.C)

        float * tab = ut_generateTab(data->nb, data->min, data->max, 0);
        //float * tab = arrFromInterval(data->nb, data->min, data->max);
        retw = write(data->fdClientToMaster, tab, data->nb * sizeof(float)); 
        myassert(retw != -1, "echec envoi tableau");
        free(tab);
    }
}

//attente de la réponse du master
void receiveAnswer(const Data *data)
{   
    //reception de la réponse du master
    int receipt; 
    int retr = read(data->fdMasterToClient, &receipt, sizeof(int));
    myassert(retr != 0, "error receiveAnswer : echec lecture accusé de recepetion");
    //printf("CLIENT : J'ai recu l'accusé de recepetion suivant %d !\n", receipt);
    
    //répartition des différents affiches et récupération des données supplémentaires 
    //(si il y en a ) auprès du master
    if (receipt == CM_ANSWER_EXIST_YES){
        int quantity;
        int retr = read(data->fdMasterToClient, &quantity, sizeof(int));
        myassert(retr != 0, "echec lecture quantite recue");

        printf("élément %f : présent en %d exemplaire(s)\n", data->elt, quantity);
    }
    else if(receipt == CM_ANSWER_EXIST_NO){
        printf("élément %f : absent \n", data->elt);
    } 
    else if(receipt == CM_ANSWER_INSERT_OK){
        printf("insertion de l'élément %f : ok\n", data->elt);
    }
    else if(receipt == CM_ANSWER_INSERT_MANY_OK){
        printf("insertion des %d éléments : ok\n", data->nb);
    }
    else if(receipt == CM_ANSWER_PRINT_OK){
        printf("affichage ok\n");
    }
    else if(receipt == CM_ANSWER_SUM_OK){
        float somme;
        int retr = read(data->fdMasterToClient, &somme, sizeof(float));
        myassert(retr != 0, "echec lecture somme recue");

        printf("somme des éléments : %f\n", somme);
    }
    else if(receipt == CM_ANSWER_MINIMUM_EMPTY){
        printf("pas de minimum\n");
    }
    else if(receipt == CM_ANSWER_MINIMUM_OK){
        float result; 
        retr = read(data->fdMasterToClient, &result, sizeof(float));
        myassert(retr != 0, "echec lecture element");

        printf("minimum : %f\n", result);
    }
    else if(receipt == CM_ANSWER_MAXIMUM_EMPTY){
        printf("pas de maximum\n");
    }
    else if(receipt == CM_ANSWER_MAXIMUM_OK){
        float result; 
        retr = read(data->fdMasterToClient, &result, sizeof(float));
        myassert(retr != 0, "echec lecture element");

        printf("maximum : %f\n", result);
    }else if (receipt == CM_ANSWER_STOP_OK){
        printf("le master s'est arrêté \n");
    }
    else if (receipt == CM_ANSWER_HOW_MANY_OK){
        int nbTotal;
        retr = read(data->fdMasterToClient, &nbTotal, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");

        int nbDistinct;
        retr = read(data->fdMasterToClient, &nbDistinct, sizeof(int));
        myassert(retr != 0, "echec lecture cardinalité");

        printf("il y a %d élément(s) \nil ya %d élément(s) distinct(s)\n", nbTotal, nbDistinct);
    }
}


/************************************************************************
 * Fonction principale
 ************************************************************************/
int main(int argc, char * argv[])
{
    Data data;
    parseArgs(argc, argv, &data);

    if (data.order == CM_ORDER_LOCAL)
        lauchThreads(&data);
    else
    {
        // - entrée en section critique
        struct sembuf operationMoins = {0, -1, 0}; //definition des opérations sur les sémaphores
        struct sembuf operationPlus = {0, +1, 0};
        
        int semId1 = semget(KEY1, 1, 0);
        myassert(semId1 != -1, "error main client.c : echec ouverture sema 1");

        int semId2 = semget(KEY2, 1, 0);
        myassert(semId2 != -1, "error main client.c : echec ouverture sema 2");

        int retsem2 = semop(semId2, &operationMoins, 1);
        myassert(retsem2 != -1, "error main client.c : echec 'vendre' sémaphore");
        
        // - ouverture des tubes nommés
        data.fdClientToMaster = open("pipe1", O_WRONLY);
        myassert(data.fdClientToMaster  != -1, "error main client.c : echec ouverture ecriture pipe ClientToMaster");
        //printf("CLIENT : ouverture écriture ClienToMaster ok !\n");

        data.fdMasterToClient = open("pipe2", O_RDONLY);
        myassert(data.fdClientToMaster != -1, "error main client.c : echec ouverture lecture pipe MasterToClient");
        //printf("CLIENT : ouverture lecture MasterToClient ok !\n");

        sendData(&data);
        receiveAnswer(&data);

        // - débloque le master
        int retsem1 = semop(semId1, &operationPlus, 1);
        myassert(retsem1 != -1, "error main client.c : echec 'acheter' sémaphore 1");

        // - libération des ressources (fermeture des tubes) 
        int ret1 = close(data.fdClientToMaster);
        myassert(ret1 == 0, "error main client : echec fermeture pipe ClientTomaster");

        int ret2 = close(data.fdMasterToClient);
        myassert(ret2 == 0, "error main client : echec fermeture pipe MasterToClient");

        //sortie de la section critique
        //on rend le sémaphore après la bonne execution des closes
        retsem2 = semop(semId2, &operationPlus, 1);
        myassert(retsem2 != -1, "error main client.c : echec 'acheter' sémaphore 2");
    }
    return EXIT_SUCCESS;
}
