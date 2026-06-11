set(LLVM_LINK_COMPONENTS
  Core
  TargetParser
  TextAPI
  )

add_llvm_unittest(TextAPITests
  TextStubV1Tests.cpp
  TextStubV2Tests.cpp
  TextStubV3Tests.cpp
  TextStubV4Tests.cpp
  TextStubV5Tests.cpp
  RecordTests.cpp
  TextAPICTest.cpp
  )

target_link_libraries(TextAPITests PRIVATE LLVMTestingSupport)
