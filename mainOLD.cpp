//
// Created by Filippo Casari on 27.04.23.
//
#include <fstream>
#include <iostream>
#include <thread>
#include <sstream>
#include <vector>
#include <tuple>
#include <utility>
#include "Particle.h"
#include <functional>
#include <unordered_set>
#include <czmq.h>
#include <csignal>
#include <omp.h>
#include <chrono>

using namespace std;
volatile bool stop = false;
void interrupt_handler(int signal_num)
{
    stop = true;
}
double *return_velocities(double init_temp, int N, double m) {

    double init_total_kinetic_energy = 1.5 * ((double) N) * init_temp;

    const double individual_kin_energy = init_total_kinetic_energy / (double) N;
    //velocities = np.full(N, np.sqrt(2*individual_kin_energy/m));
    auto *velocities = new double[N];
    for (int i = 0; i < N; ++i) {
        velocities[i] = sqrt(2.0 * individual_kin_energy / m);
    }
    printf("Initial kinetics energy : %f\n", init_total_kinetic_energy);
    printf("Individual kinetics energy : %f\n", individual_kin_energy);

    return velocities;
}

double compute_init_potential_energy(std::vector<Particle> particles, double L, double rc) {
    double potential_energy = 0.0;
    double rc6 = pow(rc, 6);
    double rc12 = rc6 * rc6;
    int counter = 0;
    for (int i = 0; i < particles.size() - 1; i++) {
        auto part = particles[i];
        double x = part.x, y = part.y;

        for (int j = i + 1; j < particles.size(); j++) {
            auto part2 = particles[j];
            double d_x = x - part2.x;
            if (d_x > L / 2) {
                d_x -= (double) L;
            } else if (d_x <= -L / 2) {
                d_x += (double) L;
            }
            double d_y = y - part2.y;
            if (d_y > L / 2) {
                d_y -= (double) L;
            } else if (d_y <= -L / 2) {
                d_y += (double) L;
            }

            double r = sqrt(d_x * d_x + d_y * d_y);
            //std::cout << " r init: " << r << endl;
            double r6 = pow(r, 6);
            double r12 = r6 * r6;
            counter++;
            if (r <= rc) {
                //std::cout<< " Potential energy: " << potential_energy << endl;
                //cout<< "value to add "<< 4 * (1.0/(r12) - 1.0/(r6)) - 4*((1.0/(rc12)) - (1.0/(rc6))) << endl;
                potential_energy += 4 * (1.0 / (r12) - 1.0 / (r6)) - 4 * ((1.0 / (rc12)) - (1.0 / (rc6)));
            }


        }
    }

    std::cout << "unique interactions: " << counter << endl;
    return potential_energy;
}

double compute_init_kinetic_energy(const std::vector<Particle> &particles) {
    double kinetic_energy = 0.0;
    for (auto &particle: particles) {
        kinetic_energy += particle.k_en;
    }
    return kinetic_energy;
}

struct PairHash {
    template<class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};
static void publisher_actor(zsock_t *pipe, void *args)
{
    zsock_t *pub = zsock_new_pub("tcp://*:5556");
    assert(pub);
    zsock_signal(pipe, 0);
    while (!zsys_interrupted) {
        // Read message from the pipe
        zmsg_t *msg = zmsg_recv(pipe);
        //cout<<"message received"<<endl;

        // Publish message
        zmsg_send(&msg, pub);
        zmsg_destroy(&msg);
    }

    zsock_destroy(&pub);
}


int main(int argc, char* argv[]) {
    const char* num_threads = getenv("OMP_NUM_THREADS");
    if (num_threads != nullptr) {
        std::cout << "Using " << num_threads << " threads with OpenMP" << std::endl;
    } else {
        std::cout << "OpenMP not enabled" << std::endl;
    }
    printf("Assignment 3\n");
    cout<<"Arguments to pass: N,  init temperature ,thermostat temp, verbose (optional, -v)"<<endl;
    signal(SIGINT, interrupt_handler);
    int N = static_cast<int>(std::strtol(argv[1], nullptr, 10));
    //std::cout << "Insert N: \n";
    //std::cin >> N;
    // Enable point size control in the vertex shader
    cout<<"N: " << N << endl;
    bool verbose;
    if(argv[4] != nullptr && strcmp(argv[4], "-v") == 0){
        cout<<"verbose: " << "True" << endl;
        verbose = true;
    }else{
        cout<<"verbose: " << "False" << endl;
        verbose = false;
    }



    double L = 30;

    double m = 1.0;
    double rc = 2.5;
    double dt = 0.001;

    double thermostat_temp = std::strtod(argv[3], nullptr);
    cout<<"thermostat temp: " << thermostat_temp << endl;
    //std::cout << "Insert thermostat temperature: ";
    //std::cin >> thermostat_temp;
    int N_X = static_cast<int>(L / rc);
    int N_Y = static_cast<int>(L / rc);
    printf("N_X = %d\n", N_X);

    std::vector<Particle> particle_list;

    double *velocities;
    double init_temp = std::strtod(argv[2], nullptr);
    cout<<"init temp: " << init_temp << endl;
    //std::cout << "Insert thermostat temperature: ";
    //std::cin >> init_temp;
    velocities = return_velocities(init_temp, N, m);


    std::vector<std::pair<double, double>> positions;
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < L; j++) {
            positions.emplace_back(static_cast<double> (i ), static_cast<double> (j));
        }

    };
    printf("size vector particles: %lu\n", particle_list.size());
    //for (int i=0;i<(positions.size());i++){
    //cout << "position "<< i << " = " << positions[i].first << ", "<<positions[i].second << endl;

    //};

    for (int i = 0; i < N; i++) {
        double alpha = 2.0 * 3.14 * rand();
        int indx = rand() % positions.size();
        double pos_x = positions[indx].first;
        double pos_y = positions[indx].second;
        positions.erase(positions.begin() + indx);

        double x = pos_x;
        double y = pos_y;
        double vx = velocities[i] * cos(alpha);
        double vy = velocities[i] * sin(alpha);
        int xcell = static_cast<int>(x / rc);
        int ycell = static_cast<int>(y / rc);
        if(verbose){
            cout << "xcell: " << xcell << " = " << x << " / " << rc << endl;
            cout << "ycell: " << ycell << " = " << y << " / " << rc << endl;
        }

        particle_list.emplace_back(x, y, vx, vy, m, L, xcell, ycell, thermostat_temp);

    };

    std::unordered_set<std::pair<double, double>, PairHash> unique_positions;
    for (const auto &part: particle_list) {
        unique_positions.insert(std::make_pair(part.x, part.y));
    }
    if (unique_positions.size() == particle_list.size()) {
        std::cout << "All particles have unique coordinates" << std::endl;
    } else {
        std::cout << "Some particles have duplicate coordinates" << std::endl;
    }
    if(verbose){
        for (int i = 0; i < particle_list.size(); i++) {
            cout << "particle " << i << " position = " << particle_list[i].x << ", " << particle_list[i].y << endl;
            cout << "particle " << i << " velocities = " << particle_list[i].vx << ", " << particle_list[i].vy << endl;
            cout << "particle " << i << " cell = " << particle_list[i].i << ", " << particle_list[i].j << endl;
        };
    }

    double init_potential_energy = compute_init_potential_energy(particle_list, L, rc);
    cout << "Initial potential energy: " << init_potential_energy << endl;
    cout << "Initial kinetic energy: " << compute_init_kinetic_energy(particle_list) << endl;
    std::vector<std::vector<std::vector<Particle>>> cell_list(N_X, std::vector<std::vector<Particle>>(N_Y,
                                                                                                      std::vector<Particle>()));
    for (auto &i: particle_list) {
        cell_list[i.i][i.j].push_back(i);
    };


    vector<double> x_array = vector<double>(particle_list.size());
    vector<double> y_array = vector<double>(particle_list.size());
    for (int i = 0; i < particle_list.size(); i++) {
        x_array[i] = particle_list[i].x;
        y_array[i] = particle_list[i].y;
    }


    //zsock_t *socket = zsock_new_req("tcp://localhost:5556");
    zactor_t *publisher = zactor_new(publisher_actor, nullptr);
    zmsg_t *reply;
    zmsg_t *message = zmsg_new();
    double T = init_temp;
    zframe_t *frame_x;
    zframe_t *frame_y;
    zframe_t *frame_t;
    zframe_t *frame_pot_en;
    zframe_t *frame_k_en;
    zframe_t *frame_m;

    int iter = -1;
    auto start_time = std::chrono::high_resolution_clock::now();
    while (!stop && !zsys_interrupted && iter<600) {
        iter++;

        message = zmsg_new();
        //cell_list.clear();


        for (auto &part: particle_list) {
            int i_old = part.i;
            int j_old = part.j;
            double x_old = part.x;
            double y_old = part.y;
            if(init_temp != thermostat_temp){
                part.apply_thermostat_scaling(T);
            }
            //part.apply_thermostat_scaling(T);

            part.update(dt, rc);

            int x_cell = part.i;
            int y_cell = part.j;
            //cout<< "i old "<< i_old<< " j old "<< j_old<<endl;
            //cout<< "i new "<< x_cell<< " j new "<< y_cell<<endl;


            //cout << "size cell old : " << cell_list[i_old][j_old].size() << endl;

            cell_list[x_cell][y_cell].push_back(part);

            int in = -1;
            for (int i = 0; i < cell_list[i_old][j_old].size(); i++) {
                if (cell_list[i_old][j_old][i].x == x_old && cell_list[i_old][j_old][i].y == y_old &&
                    cell_list[i_old][j_old][i].i == i_old && cell_list[i_old][j_old][i].j == j_old) {
                    in = i;
                    //cout << "found" << endl;
                    break;
                }
            }
            //cout << in << endl;
            if (in != -1) {
                cell_list[i_old][j_old].erase(cell_list[i_old][j_old].begin() + in);
                //cout << "size cell new : " << cell_list[i_old][j_old].size() << endl;
            } else {
                cout << "not found" << endl;
            }


        }


        double potential_energy = 0.0;
        double rc6 = pow(rc, 6);
        double rc12 = rc6 * rc6;


        int iterations = 0;
//#pragma omp parallel for reduction(+:potential_energy)
        for (auto &part: particle_list) {

            double x = part.x, y = part.y;
            int cell_x = part.i;
            int cell_y = part.j;


            double Fx = 0.0;
            double Fy = 0.0;
            iterations++;
            for (int i = -1; i < 2; i++) {

                for (int j = -1; j < 2; j++) {
                    int cell_x_new = (cell_x + i) % N_X;
                    int cell_y_new = (cell_y + j) % N_Y;
                    if (cell_x_new < 0) {
                        cell_x_new += N_X;
                    }

                    if (cell_y_new < 0) {
                        cell_y_new += N_Y;
                    }
                    //cout<<"i : "<<cell_x_new<< " j: "<< cell_y_new<< endl;

                    for (auto &part2: cell_list[cell_x_new][cell_y_new]) {

                        double d_x = x - part2.x;
                        if (d_x > L / 2.0) {
                            d_x -= L;
                        } else if (d_x <= -L / 2.0) {
                            d_x += L;
                        }
                        double d_y = y - part2.y;
                        if (d_y > L / 2.0) {
                            d_y -= L;
                        } else if (d_y <= -L / 2.0) {
                            d_y += L;
                        }

                        double r = std::sqrt(d_x * d_x + d_y * d_y);
                        //std::cout<< " r: " << r << endl;

                        if (r <= rc && r > 0.0) {
                            double r6 = pow(r, 6);
                            double r12 = r6 * r6;
                            double r2 = r * r;
                            Fx += ((48.0 * (d_x / r)) / r2) * (1.0 / r12 - 0.5 / r6);
                            Fy += ((48.0 * (d_y / r)) / r2) * (1.0 / r12 - 0.5 / r6);
                            //std::cout<< " Potential energy: " << potential_energy << endl;
                            //cout<< "value to add "<< 4 * (1.0/(r12) - 1.0/(r6)) - 4*((1.0/(rc12)) - (1.0/(rc6))) << endl;
                            potential_energy += 4.0 * (1.0 / (r12) - 1.0 / (r6)) - 4 * ((1.0 / (rc12)) - (1.0 / (rc6)));
                        }
                    }
                }

            }
            //cout<<"F x : "<<Fx<< " , Fy : "<< Fy<<endl;
            part.update_vel_acc(dt, Fx, Fy);
        }
        //cout<< "Iterations "<< iterations<< endl;
        double kinetic_energy = 0;
        double momentum =0;
        double sum_vx = 0;
        double sum_vy = 0;
//#pragma omp parallel for reduction(+:kinetic_energy,sum_vx,sum_vy)
        for (int i = 0; i < particle_list.size(); i++) {
            x_array[i] = particle_list[i].x;
            //cout<<"x["<<i<<"] = "<<x_array[i]<<endl;
            y_array[i] = particle_list[i].y;
            kinetic_energy += particle_list[i].k_en;
            //v =sqrt(particle_list[i].vx*particle_list[i].vx + particle_list[i].vy*particle_list[i].vy);
            sum_vx+=particle_list[i].vx;
            sum_vy+=particle_list[i].vy;

        }
        momentum = sqrt(sum_vx*sum_vx + sum_vy*sum_vy)*m;
        T = 2.0*kinetic_energy/(3.0*N);

        //cout<<"kinetic energy already computed"<< endl;
        frame_x = zframe_new(x_array.data(), sizeof(double) * x_array.size());
        zmsg_add(message, frame_x);
        frame_y = zframe_new(y_array.data(), sizeof(double) * y_array.size());
        zmsg_add(message, frame_y);
        frame_pot_en = zframe_new(&potential_energy, sizeof(double));
        zmsg_add(message, frame_pot_en);
        frame_k_en = zframe_new(&kinetic_energy, sizeof(double));
        zmsg_add(message, frame_k_en );
        frame_m = zframe_new(&momentum, sizeof(double));
        zmsg_add(message, frame_m);
        frame_t = zframe_new(&T, sizeof(double));
        zmsg_add(message, frame_t);


        zmsg_send(&message, publisher);

        //zframe_t *reply_frame = zmsg_pop(reply);


        //zframe_destroy(&reply_frame);
        //auto end_time = std::chrono::high_resolution_clock::now();
        //auto elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        //std::cout << "Elapsed time: " << elapsed_time << " nanoseconds" << std::endl;

    }
    auto end = std::chrono::high_resolution_clock::now(); // get end time
    auto duration = (end - start_time); // calculate duration

    cout << "Execution time: " << duration.count() << " microseconds" << endl;
    zactor_destroy(&publisher);
    //zmsg_destroy(&reply);
    cout<<"AS3 completed"<<endl;
    return 0;
}

