#include <bits/stdc++.h>
using namespace std;

int main(){
    vector<int> a{1, 2, 3, 4, 5};
    vector<int> b(a.begin() + 3, a.end());
    for(auto v : b){
        cout<<v<<" ";
    }
    cout<<endl;
}