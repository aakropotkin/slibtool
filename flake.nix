{
  description = "A surrogate libtool implementation";
  outputs = { self, nixpkgs, ... }: {

    overlays.slibtool = import ./overlay.nix;
    overlay = self.overlays.slibtool;

    packages.x86_64-linux.slibtool = ( import nixpkgs {
      sys = "x86_64-linux";
      overlays = [self.overlay];
    } ).slibtool;
    defaultPackage.x86_64-linux = self.packages.x86_64-linux.slibtool;

    nixosModules.slibtool = { pkgs, ... }: {
      nixpkgs.overlays = [self.overlay];
    };
    nixosModule = self.nixosModules.slibtool;
  };
}
