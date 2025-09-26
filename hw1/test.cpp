#include <stdio.h>
#include <string.h>
#include<bits/stdc++.h>
using namespace std;

int main() {
    string st="Congratulations! you son";
    cout<<st;
    if(st.size()>=17 && st.compare(0,17,"Congratulations!")==0){
        cout<<"returned"<<endl;
        return 1;
    }

    return 0;
}