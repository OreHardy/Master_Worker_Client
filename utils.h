#ifndef UTILS_H
#define UTILS_H

//TODO d'autres include éventuellement


/******************************************
 * nombres aléatoires
 ******************************************/
// float aléatoire entre min et max (max non inclus : [min,max[), arrondi à <precision> chiffre(s) après la virgule
float ut_getAleaFloat(float min, float max, int precision);

// tableau de float aléatoires utilisant la fonction ci-dessus
float * ut_generateTab(int size, float min, float max, int precision);

// tableau de float depuis un intervalle donné 
float * arrFromInterval(int nb, float min, float max);

#endif
