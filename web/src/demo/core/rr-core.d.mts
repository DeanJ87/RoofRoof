// Types for the generated device core (tools/demo-core/build.sh).
interface RoofCoreModule {
  EmulatedDevice: new (identityJson: string) => unknown;
}
declare const createRoofCore: (options?: Record<string, unknown>) => Promise<RoofCoreModule>;
export default createRoofCore;
