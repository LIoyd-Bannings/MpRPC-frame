#include<stdio.h>

void hello()
{
	printf("hello echo~\n");
}


int main()
{

	int arr[4]={1,2,3,4};
	int i=0;
	for(;i<4;i++)
	{
		printf("%d\n",arr[i]);
	}
	hello();
	return 0;

}
