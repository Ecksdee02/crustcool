// makegrid2.cc
//
// Makes envelope models, automatically try different helium depths
//
 
#include <stdio.h>
#include "../h/envelope2.h"

//------------------------------------------------------------------------


int main(void)
{
	double Bfield;
	
	printf("Enter B field in G (0 for unmagnetized)..."); scanf("%lg",&Bfield);

	Envelope envelope2;
	envelope2.use_potek_eos_in_He=0;
	envelope2.use_potek_cond_in_He=0;
	envelope2.use_potek_eos_in_Fe=0;
	envelope2.use_potek_cond_in_Fe=0;
	if (Bfield > 0.0) envelope2.use_potek_kff=1;
	else envelope2.use_potek_kff=0;

    envelope2.make_grid(Bfield);   // results are in "envelope_data/grid"
}
