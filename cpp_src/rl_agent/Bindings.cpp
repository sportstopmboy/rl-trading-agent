// This code is responsible for translating the C++ code to Python
// This allows us to use popular Python RL libraries without having to implement them from scratch in C++
// This is done through pybind11
// Rather cool library :)

#include <resources/libraries/pybind11/pybind11.h>
#include <resources/libraries/pybind11/stl.h>
#include "cpp_src/environment/Environment.h" 

namespace py = pybind11;

// "rl_volatility_engine" is the name of the module to import in Python
PYBIND11_MODULE(rl_volatility_engine, m) 
{
    m.doc() = "C++ Options Trading Environment for PyTorch RL";

    // Bind the StepResult struct
    // We use def_readonly so Python can read the results, but can't accidentally overwrite them
    py::class_<StepResult>(m, "StepResult")
        .def_readonly("state_features", &StepResult::stateFeatures)
        .def_readonly("reward", &StepResult::reward)
        .def_readonly("is_done", &StepResult::isDone)
        .def_readonly("current_nav", &StepResult::currentNAV);

    // Bind the Environment class
    py::class_<Environment>(m, "Environment")
        // Bind the constructor (Requires the initial cash parameter)
        .def(py::init<double>(), py::arg("initial_cash"))
        
        // Bind the core RL functions
        .def("reset", &Environment::reset)
        .def("step", &Environment::step, py::arg("agent_actions"))
        .def("get_is_done", &Environment::getIsDone);
}