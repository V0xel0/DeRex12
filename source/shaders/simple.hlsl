
// vertex
struct VSIntput
{
	float4 position : POSITION;
	float4 color : COLOR;
};

struct VSOutput
{
	// data to output to the next stage
};
 

// Explicitly declare the input vars as params of the entry point.
// Therefore, the VSIntput struct is useless in this case.
VSOutput VSMain(float4 position : POSITION, float4 color : COLOR)
{
	// Use position and color to output the data that the next stage expects as input
}