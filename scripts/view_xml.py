import sys

import xml.etree.ElementTree as ET
import plotly.graph_objects as go
import networkx as nx


def build_plot(G: nx.Graph) -> go.Figure:
    """
    Builds a Plotly figure representing a NetworkX graph.

    Parameters:
    G (nx.Graph): The NetworkX graph to be visualized.

    Returns:
    go.Figure: A Plotly figure object containing the visual representation of the graph.

    The function uses a spring layout to position the nodes and creates two traces:
    one for the edges and one for the nodes. The nodes are displayed as markers with
    their labels, and the edges are displayed as lines.
    """
    pos = nx.spring_layout(G, seed=42)  # 'seed' for reproducible node positions

    # Extract metadata from the graph and add it to the node attributes
    for node in G.nodes():
        G.nodes[node]['BOUNDARY_CONDITION'] = G.nodes[node].get('BOUNDARY_CONDITION', None)
        G.nodes[node]['MATERIAL'] = G.nodes[node].get('MATERIAL', None)
        G.nodes[node]['num_triangles'] = G.nodes[node].get('num_triangles', 0)

    # Attach the layout positions to each node's "pos" attribute
    for node in G.nodes():
        G.nodes[node]['pos'] = pos[node]

    edge_x = []
    edge_y = []
    for edge in G.edges():
        x0, y0 = G.nodes[edge[0]]['pos']
        x1, y1 = G.nodes[edge[1]]['pos']
        edge_x.extend([x0, x1, None])
        edge_y.extend([y0, y1, None])

    edge_trace = go.Scatter(
        x=edge_x,
        y=edge_y,
        line=dict(width=1, color='#888'),
        hoverinfo='none',
        mode='lines'
    )

    node_x = []
    node_y = []
    node_text = []
    hover_info = []
    for node, data in G.nodes(data=True):
        x, y = data['pos']
        node_x.append(x)
        node_y.append(y)
        node_text.append(str(node))
        num_triangles = data.get('num_triangles', 0)
        boundary_condition = data.get('BOUNDARY_CONDITION', None)
        material = data.get('MATERIAL', None)
        node_info = f"{node}<br>Triangles: {num_triangles}"
        if 'surface' in str(node).lower() and boundary_condition != None:
            node_info += f"<br>Boundary Condition: {boundary_condition}"
        if 'volume' in str(node).lower() and material != None:
            node_info += f"<br>Material: {material}"
        hover_info.append(node_info)

    node_trace = go.Scatter(
        x=node_x,
        y=node_y,
        mode='markers+text',
        text=node_text,
        textposition='top center',
        hoverinfo='text',
        hovertext=hover_info,
        marker=dict(
            showscale=False,
            color='#636EFA',
            size=10,
            line_width=2
        )
    )
    fig = go.Figure(
    data=[edge_trace, node_trace],
    layout=go.Layout(
        title='XDG Model Topology',
        showlegend=False,
        hovermode='closest'
        )
    )

    fig.update_layout(
        xaxis=dict(showgrid=False, zeroline=False, visible=False),
        yaxis=dict(showgrid=False, zeroline=False, visible=False)
    )

    return fig


if __name__ == "__main__":
    G = nx.read_graphml(sys.argv[1])
    fig = build_plot(G)
    fig.show()
