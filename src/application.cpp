#include "ezgl/application.hpp"

namespace ezgl {

application create_application(int argc, char **argv)
{
  // initialize gtk - will abort application if it fails
  gtk_init(&argc, &argv);

  return application();
}
}
