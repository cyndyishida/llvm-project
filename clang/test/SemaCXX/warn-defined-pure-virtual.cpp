// RUN: %clang_cc1 -fsyntax-only -Wdefined-pure-virtual -verify %s

struct B1 {
  virtual void foo() = 0; // expected-note {{declared here}}
};

void B1::foo() { // expected-warning {{definition for pure virtual function}}
}
