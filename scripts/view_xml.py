import sys

import xml.etree.ElementTree as ET
import plotly.graph_objects as go
import networkx as nx
from networkx.drawing.nx_agraph import to_agraph


def graph_from_xml(xml_file: str = "xdg_model.xml") -> nx.Graph:

    # # Parse the XML
    # tree = ET.parse(xml_file)
    # root = tree.getroot()

    # Create a Graph
    G = nx.read_graphml(xml_file, )
    return G

    # 1) Extract nodes (with coordinates)
    nodes_element = root.find("nodes")  # or adjust based on your XML structure
    for node_el in nodes_element.findall("node"):
        node_id = node_el.get("id")
        x_coord = float(node_el.get("x"))
        y_coord = float(node_el.get("y"))
        # Add node to the graph
        G.add_node(node_id, pos=(x_coord, y_coord))

    # 2) Extract edges
    edges_element = root.find("edges")  # or adjust based on your XML structure
    for edge_el in edges_element.findall("edge"):
        source = edge_el.get("source")
        target = edge_el.get("target")
        # Add edge to the graph
        G.add_edge(source, target)

    # (Optional) If your data has 3D coordinates (x, y, z), store them similarly
    # G.add_node(node_id, pos_3d=(x_coord, y_coord, z_coord))
    return G


def build_plot(G: nx.Graph) -> go.Figure:
    pos = nx.spring_layout(G, seed=42)  # 'seed' for reproducible node positions

    G = to_agraph(G)
    G.layout(prog='dot')
    G.draw('graph.png')

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
    for node, data in G.nodes(data=True):
        x, y = data['pos']
        node_x.append(x)
        node_y.append(y)
        node_text.append(str(node))

    node_trace = go.Scatter(
        x=node_x,
        y=node_y,
        mode='markers+text',
        text=node_text,
        textposition='top center',
        hoverinfo='text',
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
        title='Interactive NetworkX Graph from XML',
        showlegend=False,
        hovermode='closest'
        )
    )

    return fig


if __name__ == "__main__":
    G = graph_from_xml(sys.argv[1])
    fig = build_plot(G)
    fig.show()