/*
C API lifetime test: the same Soloud pointer must only be destroyed once.
*/

#include <stdio.h>

#include "soloud_c.h"

int main()
{
	Soloud *instance = Soloud_create();
	if (instance == 0)
		return 2;

	int first = Soloud_destroy(instance);
	int second = Soloud_destroy(instance);
	int nullDestroy = Soloud_destroy(0);

	printf("first=%d second=%d null=%d\n", first, second, nullDestroy);
	return (first == 0 && second == 1 && nullDestroy == 1) ? 0 : 1;
}
