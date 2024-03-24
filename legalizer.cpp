#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <limits>
#include <set>
#include <random>
using namespace std;

//global variable
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int> distribution(0,INT32_MAX);
std::uniform_real_distribution<double> real_distribution(0.0, 1.0);
auto max_duration = std::chrono::seconds(580);
auto output_duration = std::chrono::seconds(10);

class instance {
public:
    const int id;
    const string type;
    const string name;
    int place; // resource id, if haven't found, set -1
    double x, y;
    vector<int> netlist; // store net's id which is connected with this instance

    instance(int _id, string _name, string _type, double _x, double _y)
        : id(_id), name(_name), type(_type), place(-1), x(_x), y(_y){};

    void output() {
        cout << "id:" << id << " x:" << x << " y:" << y << " type:" << type << " name:"
            << name << " place:" << place << endl;
    }
};

class resource {
public:
    const int id;
    const double x, y; // coordinate
    const string type;
    const string name;

    int ins_id; // the instance's id which is set on this resource, init = -1
    instance* ins_pointer;
    resource(int _id, string _type, string _name, double _x, double _y)
        : id(_id), type(_type), name(_name), x(_x), y(_y), ins_id(-1), ins_pointer(nullptr){};

    void output() {
        cout << "id:" << id << " x:" << x << " y:" << y << " type:" << type << " name:" << name << " ins_id:" <<
            ins_id << endl;
        return;
    }
    void update_ins(){
        if(ins_pointer == nullptr)return;
        ins_pointer->place=id;
        ins_pointer->x=x;
        ins_pointer->y=y;
        return;
    }
};

class net {
public:
    int id;
    string name;
    vector<int> net_element;
    vector<instance*> element_pointer;
    double top, bottom, left, right;
    int flag; // for HPWL, check whether the four values are initialized
    net(int _id, string _name) : id(_id), name(_name), flag(0) {}

    double HPWL() {
        if (!flag) {
            recomputebound();
            flag = 1;
        }
        return (top - bottom) + (right - left);
    }
    void recomputebound() {
        for (auto it = element_pointer.begin(); it != element_pointer.end(); it++) {
            if (it == element_pointer.begin()) {
                top = bottom = (**it).y;
                left = right = (**it).x;
            }
            if (left > (**it).x) left = (**it).x;
            if (right < (**it).x) right = (**it).x;
            if (top < (**it).y) top = (**it).y;
            if (bottom > (**it).y) bottom = (**it).y;
        }
    }
};

class FPGA {
public:
    vector<resource> FPGAarc;
    vector<int> CLB, DSP, RAM;
    vector<instance> FPGAcell;
    vector<net> FPGAnet;
    unordered_map<string, int> inst_nameToId;

    // Constructor to initialize the FPGA object
    FPGA() {}

    // Function to add a new resource to FPGA
    void addResource(const resource& newelement) {
        FPGAarc.push_back(newelement);
        if (newelement.type == "CLB") CLB.push_back(newelement.id);
        else if (newelement.type == "DSP") DSP.push_back(newelement.id);
        else if (newelement.type == "RAM") RAM.push_back(newelement.id);
        else {
            cerr << "error: unknown resource type" << endl;
        }
    }

    // Function to add a new instance to FPGA
    void addInstance(const instance& newinst) {
        inst_nameToId[newinst.name] = newinst.id;
        FPGAcell.push_back(newinst);
    }

    // Function to add a new net to FPGA
    void addNet(const net& new_net) {
        FPGAnet.push_back(new_net);
    }

    //move source to target,if target isn't empty,swap then,and return delta_HPWL
    double swap(int source_id,int target_id){
        if(FPGAarc[source_id].type != FPGAarc[target_id].type){
            cerr<<"error:swap different type module"<<endl;
            return 0;
        }
        if(FPGAarc[source_id].type == "IO" || FPGAarc[target_id].type == "IO"){
            cerr<<"error:swap IO is ban"<<endl;
            return 0;
        }
        int temp_id=FPGAarc[source_id].ins_id;
        instance* temp_ptr=FPGAarc[source_id].ins_pointer;
        FPGAarc[source_id].ins_id=FPGAarc[target_id].ins_id;
        FPGAarc[source_id].ins_pointer=FPGAarc[target_id].ins_pointer;
        FPGAarc[target_id].ins_id=temp_id;
        FPGAarc[target_id].ins_pointer=temp_ptr;

        FPGAarc[source_id].update_ins();
        FPGAarc[target_id].update_ins();

        set<int> netset;
        if(FPGAarc[source_id].ins_pointer != nullptr){
            for(auto it=FPGAarc[source_id].ins_pointer->netlist.begin();
            it != FPGAarc[source_id].ins_pointer->netlist.end();it++){
                netset.insert(*it);
            }
        }
        if(FPGAarc[target_id].ins_pointer != nullptr){
            for(auto it=FPGAarc[target_id].ins_pointer->netlist.begin();
            it != FPGAarc[target_id].ins_pointer->netlist.end();it++){
                netset.insert(*it);
            }
        }
        double old_hpwl=0,new_hpwl=0;
        for(auto it=netset.begin();it != netset.end();it++){
            old_hpwl+=FPGAnet[*it].HPWL();
            FPGAnet[*it].flag=0;
            new_hpwl+=FPGAnet[*it].HPWL();
        }
        return new_hpwl-old_hpwl;
    }

    double total_HPWL(){
        double sum=0;
        for(auto it=FPGAnet.begin();it != FPGAnet.end();it++){
            sum+=it->HPWL();
        }
        return sum;
    }

    int randomvalue(int leftbound,int rightbound){
        return leftbound+distribution(gen)%(rightbound-leftbound);
    }

    void SA(std::chrono::high_resolution_clock::time_point start_time){
        double T=1;
        int counter=0;
        auto cut = std::chrono::high_resolution_clock::now();
        while(true){
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
            if(duration>max_duration)break;
            auto cut_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - cut);
            if(cut_duration > output_duration){
                static int counter=0;
                counter++;
                cout <<counter<< ":total HPWL_net:" << total_HPWL() << endl;
                cut=now;
                T*=0.90;
                cout<<"T_value:"<< T <<endl;
            }
            int moved=randomvalue(0,FPGAcell.size()-1);
            moved=FPGAcell[moved].place;
            if(-1 == moved)continue;
            int terminal=randomvalue(0,FPGAarc.size()-1);
            if(FPGAarc[moved].type == "CLB"){
                terminal=CLB[randomvalue(0,CLB.size()-1)];
            }
            else if(FPGAarc[moved].type == "RAM"){
                terminal=RAM[randomvalue(0,RAM.size()-1)];
            }
            else if(FPGAarc[moved].type == "DSP"){
                terminal=DSP[randomvalue(0,DSP.size()-1)];
            }
            else{
                cerr<<"error: unknown type in "<<__func__<<endl;
            }

            double deltaC=swap(moved,terminal);
            if(counter<10){
                counter++;
                T+=deltaC;
                if(counter == 10){
                    T/=100;
                }
            }
            if(!accept(deltaC,T))swap(moved,terminal);
        }

    }
    double accept(double DeltaC,double T){
        if(DeltaC<=0)return 1;
        else if(exp(-DeltaC/T)>real_distribution(gen))return 1;
        else return 0;
    }
};

int place(resource& objA, instance& objB) {
    objB.place = objA.id;
    objB.x = objA.x;
    objB.y = objA.y;
    objA.ins_id = objB.id;
    objA.ins_pointer = &objB;
    return 0;
}

int main(int argc, char* argv[]) {
    std::random_device rd;
    std::mt19937 gen(rd());
    auto start_time = std::chrono::high_resolution_clock::now();

    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " architecture file,instance file,net file and out file" << endl;
        return 1;
    }

    const char* ArchFilemame = argv[1];
    const char* instFilename = argv[2];
    const char* netFilename = argv[3];
    const char* outFilename = argv[4];

    ifstream Arch(ArchFilemame);
    ifstream inst(instFilename);
    ifstream netFile(netFilename);
    ofstream outfile(outFilename);
    if (!Arch.is_open()) {
        cerr << "unable to open:" << ArchFilemame << endl;
        return 1;
    }
    if (!inst.is_open()) {
        cerr << "unable to open:" << instFilename << endl;
        return 1;
    }
    if (!netFile.is_open()) {
        cerr << "unable to open:" << netFilename << endl;
        return 1;
    }
    if (!outfile.is_open()) {
        cerr << "unable to open:" << outFilename << endl;
        return 1;
    }
    cout << "file open success" << endl;

    // Create an instance of the FPGA class
    FPGA myFPGA;

    // input resource
    string temp_name, temp_type;
    double temp_x, temp_y;
    int counter = 0;
    while (Arch >> temp_name >> temp_type >> temp_x >> temp_y) {
        resource newelement(counter, temp_type, temp_name, temp_x, temp_y);
        myFPGA.addResource(newelement);
        counter++;
    }
    cout << "arch input success" << endl;
    myFPGA.FPGAarc.rbegin()->output();

    // input instance
    counter = 0;
    while (inst >> temp_name >> temp_type >> temp_x >> temp_y) {
        instance newinst(counter, temp_name, temp_type, temp_x, temp_y);
        myFPGA.addInstance(newinst);
        counter++;
    }

    // input net
    string line, net_name, element_name;
    counter = 0;
    while (getline(netFile, line)) {
        stringstream lss(line);
        lss >> net_name;
        net new_net(counter, net_name);
        while (lss >> element_name) {
            int element_id=myFPGA.inst_nameToId[element_name];
            new_net.net_element.push_back(element_id);
            new_net.element_pointer.push_back(&myFPGA.FPGAcell[element_id]);
            myFPGA.FPGAcell[element_id].netlist.push_back(counter);
        }
        myFPGA.addNet(new_net);
        counter++;
    }

    // initial placement
    cout << endl << endl << "start initial placement" << endl << endl;

    int CLB_counter = 0, RAM_counter = 0, DSP_counter = 0;
    auto it_clb = myFPGA.CLB.begin(), it_ram = myFPGA.RAM.begin(), it_dsp = myFPGA.DSP.begin();
    for (auto it = myFPGA.FPGAcell.begin(); it != myFPGA.FPGAcell.end(); it++) {
        if (it->type == "CLB") {
            place(myFPGA.FPGAarc[*it_clb], *it);
            it_clb++;
        }
        else if (it->type == "DSP") {
            place(myFPGA.FPGAarc[*it_dsp], *it);
            it_dsp++;
        }
        else if (it->type == "RAM") {
            place(myFPGA.FPGAarc[*it_ram], *it);
            it_ram++;
        }
    }

    // for(auto it=myFPGA.FPGAcell.begin();it != myFPGA.FPGAcell.end();it++){
    //     it->output();
    // }



    cout << "total HPWL_net:" << myFPGA.total_HPWL() << endl;
    myFPGA.SA(start_time);
    cout << "total HPWL_net:" << myFPGA.total_HPWL() << endl;
    

    // output file
    for (auto it = myFPGA.FPGAcell.begin(); it != myFPGA.FPGAcell.end(); it++) {
        if (it->type == "IO") continue;
        outfile << it->name << " " << myFPGA.FPGAarc[it->place].name << endl;
    }
    string command = "./verifier ", space = " ";
    command += (ArchFilemame + space);
    command += (instFilename + space);
    command += (netFilename + space);
    command += outFilename;
    //system(command.c_str());

    // end
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Execution Time: " << duration.count() / 1000.0 << " seconds" << std::endl;
}
