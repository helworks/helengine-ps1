#pragma once
namespace psyqo { class GPU; }
namespace helengine::ps1 {
/// <summary>Constructs and initializes generated Core without loading an authored scene.</summary>
void InitializeCoreBoot(psyqo::GPU& gpu);
}
