#include "pumipic_adjacency.hpp"
#include "unit_tests.hpp"
#include <psTypes.h>
#include <SellCSigma.h>
#include <SCS_Macros.h>
#include <Distribute.h>
#include <Kokkos_Core.hpp>
#include <chrono>
#include <thread>

#define NUM_ITERATIONS 1

using particle_structs::fp_t;
using particle_structs::lid_t;
using particle_structs::Vector3d;
using particle_structs::SellCSigma;
using particle_structs::MemberTypes;
using particle_structs::distribute_particles;
using particle_structs::distribute_name;
using particle_structs::elemCoords;

namespace o = Omega_h;
namespace p = pumipic;

//To demonstrate push and adjacency search we store:
//-two fp_t[3] arrays, 'Vector3d', for the current and
// computed (pre adjacency search) positions, and
//-an integer to store the 'new' parent element for use by
// the particle movement procedure
typedef MemberTypes<Vector3d, Vector3d, int > Particle;

void printTiming(const char* name, double t) {
  fprintf(stderr, "kokkos %s (seconds) %f\n", name, t);
}

void printTimerResolution() {
  Kokkos::Timer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  fprintf(stderr, "kokkos timer reports 1ms as %f seconds\n", timer.seconds());
}

typedef Kokkos::DefaultExecutionSpace exe_space;
//TODO Figure out how to template these helper fns
typedef Kokkos::View<fp_t*, exe_space::device_type> kkFpView;
/** \brief helper function to transfer a host array to a device view */
void hostToDeviceFp(kkFpView d, fp_t* h) {
  kkFpView::HostMirror hv = Kokkos::create_mirror_view(d);
  for (size_t i=0; i<hv.size(); ++i)
    hv(i) = h[i];
  Kokkos::deep_copy(d,hv);
}

typedef Kokkos::View<Vector3d*, exe_space::device_type> kkFp3View;
/** \brief helper function to transfer a host array to a device view */
void hostToDeviceFp(kkFp3View d, fp_t (*h)[3]) {
  kkFp3View::HostMirror hv = Kokkos::create_mirror_view(d);
  for (size_t i=0; i<hv.size()/3; ++i) {
    hv(i,0) = h[i][0];
    hv(i,1) = h[i][1];
    hv(i,2) = h[i][2];
  }
  Kokkos::deep_copy(d,hv);
}

void push(SellCSigma<Particle>* scs, int np, elemCoords& elems,
                        fp_t distance, fp_t dx, fp_t dy, fp_t dz) {
  Kokkos::Timer timer;
  Vector3d *scs_initial_position = scs->getSCS<0>();
  Vector3d *scs_pushed_position = scs->getSCS<1>();
  //Move SCS data to the device
  scs->transferToDevice();
  kkFpView ex_d("ex_d", elems.size);
  hostToDeviceFp(ex_d, elems.x);
  kkFpView ey_d("ey_d", elems.size);
  hostToDeviceFp(ey_d, elems.y);
  kkFpView ez_d("ez_d", elems.size);
  hostToDeviceFp(ez_d, elems.z);

  kkFp3View position_d("position_d", scs->offsets[scs->num_slices]);
  hostToDeviceFp(position_d, scs_initial_position);
  kkFp3View new_position_d("new_position_d", scs->offsets[scs->num_slices]);
  hostToDeviceFp(new_position_d, scs_pushed_position);
  
  fp_t disp[4] = {distance,dx,dy,dz};
  kkFpView disp_d("direction_d", 4);
  hostToDeviceFp(disp_d, disp);
  fprintf(stderr, "kokkos scs host to device transfer (seconds) %f\n", timer.seconds());

#if defined(KOKKOS_ENABLE_CXX11_DISPATCH_LAMBDA)  
  double totTime = 0;
  timer.reset();
  PS_PARALLEL_FOR_ELEMENTS(scs, thread, e, {
    //Can't used const construction of arrays because they require unprotected commas
    fp_t dir[3];
    dir[0] = disp_d(0)*disp_d(1);
    dir[1] = disp_d(0)*disp_d(2);
    dir[2] = disp_d(0)*disp_d(3);
    fp_t x[4];
    x[0] = ex_d(e);
    x[1] = ex_d(e+1);
    x[2] = ex_d(e+2);
    x[3] = ex_d(e+3);
    fp_t y[4];
    y[0] = ey_d(e);
    y[1] = ey_d(e+1);
    y[2] = ey_d(e+2);
    y[3] = ey_d(e+3);
    fp_t z[4];
    z[0] = ez_d(e);
    z[1] = ez_d(e+1);
    z[2] = ez_d(e+2);
    z[3] = ez_d(e+3);
    PS_PARALLEL_FOR_PARTICLES(scs, thread, pid, {
      fp_t c= 0;
      c += x[0] + y[0] + z[0];
      c += x[1] + y[1] + z[1];
      c += x[2] + y[2] + z[2];
      c += x[3] + y[3] + z[3];
      c/=4;
      new_position_d(pid,0) = position_d(pid,0) + c * dir[0];
      new_position_d(pid,1) = position_d(pid,1) + c * dir[1];
      new_position_d(pid,2) = position_d(pid,2) + c * dir[2];
    });
  });
  totTime += timer.seconds();
  printTiming("scs push", totTime);
#endif
}

void setInitialPtclCoords(Vector3d* p) {
  (void)p;
}

void setElemCoords(const o::Reals* coords, const o::LOs* r2v, elemCoords& elems) {
  (void)coords;
  (void)r2v;
  for( int i=0; i<elems.size; i++ ) {
    elems.x[i] = i;
    elems.y[i] = i;
    elems.z[i] = i;
  }
}

int main(int argc, char** argv) {
  Kokkos::initialize(argc,argv);
  printf("particle_structs floating point value size (bits): %zu\n", sizeof(fp_t));
  printf("omega_h floating point value size (bits): %zu\n", sizeof(Omega_h::Real));
  printf("Kokkos execution space memory %s name %s\n",
      typeid (Kokkos::DefaultExecutionSpace::memory_space).name(),
      typeid (Kokkos::DefaultExecutionSpace).name());
  printf("Kokkos host execution space %s name %s\n",
      typeid (Kokkos::DefaultHostExecutionSpace::memory_space).name(),
      typeid (Kokkos::DefaultHostExecutionSpace).name());
  printTimerResolution();

  if(argc != 3)
  {
    std::cout << "Usage: " << argv[0] << " <mesh> <particles per element>\n";
    exit(1);
  }

  auto lib = Omega_h::Library(&argc, &argv);
  const auto world = lib.world();
  auto mesh = Omega_h::gmsh::read(argv[1], world);
  const auto r2v = mesh.ask_elem_verts();
  const auto coords = mesh.coords();

  /* Particle data */

  const int ppe = atoi(argv[2]);
  Omega_h::Int ne = mesh.nelems();
  const int np = ppe*ne;

  //Distribute particles to elements evenly (strat = 0)
  const int strat = 0;
  fprintf(stderr, "distribution %d-%s #elements %d #particles %d\n",
      strat, distribute_name(strat), ne, np);
  int* ptcls_per_elem = new int[ne];
  std::vector<int>* ids = new std::vector<int>[ne];
  if (!distribute_particles(ne,np,strat,ptcls_per_elem, ids)) {
    return 1;
  }

  //'sigma', 'V', and the 'policy' control the layout of the SCS structure 
  //in memory and can be ignored until performance is being evaluated.  These
  //are reasonable initial settings for OpenMP.
  const int sigma = INT_MAX; // full sorting
  const int V = 1024;
  const bool debug = false;
  Kokkos::TeamPolicy<Kokkos::DefaultExecutionSpace> policy(10000, 4);
  fprintf(stderr, "Sell-C-sigma C %d V %d sigma %d\n", policy.team_size(), V, sigma);
  //Create the particle structure
  SellCSigma<Particle>* scs = new SellCSigma<Particle>(policy, sigma, V, ne, np,
						       ptcls_per_elem,
						       ids, debug);
  //Set initial positions and 0 out future position
  Vector3d *initial_position_scs = scs->getSCS<0>();
  int *flag_scs = scs->getSCS<2>();
  setInitialPtclCoords(initial_position_scs);
  (void)flag_scs; //TODO

  //create elems struct
  elemCoords elems(ne, 4, scs->C * scs->num_chunks);
  setElemCoords(&coords, &r2v, elems); //TODO - what is the layout of coords?
  
  //define parameters controlling particle motion
  //TODO - set these based on the model 
  fp_t distance = .1;
  fp_t dx = 0.2;
  fp_t dy = 0;
  fp_t dz = 0;
  fp_t length = sqrt(dx * dx + dy * dy + dz * dz);
  dx /= length;
  dy /= length;
  dz /= length;

  Kokkos::Timer timer;
  for(int iter=0; iter<NUM_ITERATIONS; iter++) {
    fprintf(stderr, "\n");
    timer.reset();
    push(scs, np, elems, distance, dx, dy, dz);
    fprintf(stderr, "kokkos scs with macros push and transfer (seconds) %f\n", timer.seconds());
  }

  //cleanup
  delete [] ptcls_per_elem;
  delete [] ids;
  delete scs;
  return 0;
}
