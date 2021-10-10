{
  description = "A surrogate libtool implementation";
  inputs.nixpkgs.follows = "nix/nixpkgs";
  outputs = { self, nix, nixpkgs, ... }: {

    overlays.slibtool = import ./overlay.nix;
    overlay = self.overlays.slibtool;

    packages.x86_64-linux.slibtool = ( import nixpkgs {
      sys = "x86_64-linux";
      overlays = [self.overlay nix.overlay];
    } ).slibtool;
    defaultPackage.x86_64-linux = self.packages.x86_64-linux.slibtool;

    nixosModules.slibtool = { pkgs, ... }: {
      nixpkgs.overlays = [self.overlay];
    };
    nixosModule = self.nixosModules.slibtool;
  };
}
