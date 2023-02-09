
RWByteAddressBuffer UAV;

[RootSignature("UAV(u0)")]
[numthreads(1,1,1)]
void main() {
  UAV.Store(0, 0xd3d12u);
}
