{
  cmake,
  ninja,
  stdenv,
}:
stdenv.mkDerivation {
  pname = "statwell";
  version = "0.1.0";
  src = ../.;

  nativeBuildInputs = [
    cmake
    ninja
  ];
  cmakeFlags = [ "-DSTATWELL_BUILD_TESTS=OFF" ];

  # Darwin's build-time bash can otherwise consult CoreFoundation for locale.
  env.LC_ALL = "C";

  meta.mainProgram = "statwell";
}
